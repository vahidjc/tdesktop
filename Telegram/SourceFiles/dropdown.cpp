/*
This file is part of Telegram Desktop,
the official desktop version of Telegram messaging app, see https://telegram.org

Telegram Desktop is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

It is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
GNU General Public License for more details.

In addition, as a special exception, the copyright holders give permission
to link the code of portions of this program with the OpenSSL library.

Full license: https://github.com/telegramdesktop/tdesktop/blob/master/LICENSE
Copyright (c) 2014-2016 John Preston, https://desktop.telegram.org
*/
#include "stdafx.h"
#include "dropdown.h"

#include "boxes/confirmbox.h"
#include "boxes/stickersetbox.h"
#include "inline_bots/inline_bot_result.h"
#include "inline_bots/inline_bot_layout_item.h"
#include "historywidget.h"
#include "localstorage.h"
#include "lang.h"
#include "mainwindow.h"
#include "apiwrap.h"
#include "mainwidget.h"

Dropdown::Dropdown(QWidget *parent, const style::dropdown &st) : TWidget(parent)
, _ignore(false)
, _selected(-1)
, _st(st)
, _width(_st.width)
, _hiding(false)
, a_opacity(0)
, _a_appearance(animation(this, &Dropdown::step_appearance))
, _shadow(_st.shadow) {
	resetButtons();

	_hideTimer.setSingleShot(true);
	connect(&_hideTimer, SIGNAL(timeout()), this, SLOT(hideStart()));

	if (cPlatform() == dbipMac || cPlatform() == dbipMacOld) {
		connect(App::wnd()->windowHandle(), SIGNAL(activeChanged()), this, SLOT(onWndActiveChanged()));
	}
}

void Dropdown::ignoreShow(bool ignore) {
	_ignore = ignore;
}

void Dropdown::onWndActiveChanged() {
	if (!App::wnd()->windowHandle()->isActive() && !isHidden()) {
		leaveEvent(0);
	}
}

IconedButton *Dropdown::addButton(IconedButton *button) {
	button->setParent(this);

	int32 nw = _st.padding.left() + _st.padding.right() + button->width();
	if (nw > _width) {
		_width = nw;
		for (int32 i = 0, l = _buttons.size(); i < l; ++i) _buttons[i]->resize(_width - _st.padding.left() - _st.padding.right(), _buttons[i]->height());
	} else {
		button->resize(_width - _st.padding.left() - _st.padding.right(), button->height());
	}
	if (!button->isHidden()) {
		if (_height > _st.padding.top() + _st.padding.bottom()) {
			_height += _st.border;
		}
		_height += button->height();
	}
	_buttons.push_back(button);
	connect(button, SIGNAL(stateChanged(int, ButtonStateChangeSource)), this, SLOT(buttonStateChanged(int, ButtonStateChangeSource)));

	resize(_width, _height);

	return button;
}

void Dropdown::resetButtons() {
	_width = qMax(_st.padding.left() + _st.padding.right(), int(_st.width));
	_height = _st.padding.top() + _st.padding.bottom();
	for (int32 i = 0, l = _buttons.size(); i < l; ++i) {
		delete _buttons[i];
	}
	_buttons.clear();
	resize(_width, _height);

	_selected = -1;
}

void Dropdown::updateButtons() {
	int32 top = _st.padding.top(), starttop = top;
	for (Buttons::const_iterator i = _buttons.cbegin(), e = _buttons.cend(); i != e; ++i) {
		if (!(*i)->isHidden()) {
			(*i)->move(_st.padding.left(), top);
			if ((*i)->width() != _width - _st.padding.left() - _st.padding.right()) {
				(*i)->resize(_width - _st.padding.left() - _st.padding.right(), (*i)->height());
			}
			top += (*i)->height() + _st.border;
		}
	}
	_height = top + _st.padding.bottom() - (top > starttop ? _st.border : 0);
	resize(_width, _height);
}

void Dropdown::resizeEvent(QResizeEvent *e) {
	int32 top = _st.padding.top();
	for (Buttons::const_iterator i = _buttons.cbegin(), e = _buttons.cend(); i != e; ++i) {
		if (!(*i)->isHidden()) {
			(*i)->move(_st.padding.left(), top);
			top += (*i)->height() + _st.border;
		}
	}
}

void Dropdown::paintEvent(QPaintEvent *e) {
	QPainter p(this);

	if (_a_appearance.animating()) {
		p.setOpacity(a_opacity.current());
	}

	// draw shadow
	QRect r(_st.padding.left(), _st.padding.top(), _width - _st.padding.left() - _st.padding.right(), _height - _st.padding.top() - _st.padding.bottom());
	_shadow.paint(p, r, _st.shadowShift);

	if (!_buttons.isEmpty() && _st.border > 0) { // paint separators
		p.setPen(_st.borderColor->p);
		int32 top = _st.padding.top(), i = 0, l = _buttons.size();
		for (; i < l; ++i) {
			if (!_buttons.at(i)->isHidden()) break;
		}
		if (i < l) {
			top += _buttons.at(i)->height();
			for (++i; i < l; ++i) {
				if (!_buttons.at(i)->isHidden()) {
					p.fillRect(_st.padding.left(), top, _width - _st.padding.left() - _st.padding.right(), _st.border, _st.borderColor->b);
					top += _st.border + _buttons.at(i)->height();
				}
			}
		}
	}
}

void Dropdown::enterEvent(QEvent *e) {
	_hideTimer.stop();
	if (_hiding) showStart();
	return TWidget::enterEvent(e);
}

void Dropdown::leaveEvent(QEvent *e) {
	if (_a_appearance.animating()) {
		hideStart();
	} else {
		_hideTimer.start(300);
	}
	return TWidget::leaveEvent(e);
}

void Dropdown::keyPressEvent(QKeyEvent *e) {
	if (e->key() == Qt::Key_Enter || e->key() == Qt::Key_Return) {
		if (_selected >= 0 && _selected < _buttons.size()) {
			emit _buttons[_selected]->clicked();
			return;
		}
	} else if (e->key() == Qt::Key_Escape) {
		hideStart();
		return;
	}
	if ((e->key() != Qt::Key_Up && e->key() != Qt::Key_Down) || _buttons.size() < 1) return;

	bool none = (_selected < 0 || _selected >= _buttons.size());
	int32 delta = (e->key() == Qt::Key_Down ? 1 : -1);
	int32 newSelected = none ? (e->key() == Qt::Key_Down ? 0 : _buttons.size() - 1) : (_selected + delta);
	if (newSelected < 0) {
		newSelected = _buttons.size() - 1;
	} else if (newSelected >= _buttons.size()) {
		newSelected = 0;
	}
	int32 startFrom = newSelected;
	while (_buttons.at(newSelected)->isHidden()) {
		newSelected += delta;
		if (newSelected < 0) {
			newSelected = _buttons.size() - 1;
		} else if (newSelected >= _buttons.size()) {
			newSelected = 0;
		}
		if (newSelected == startFrom) return;
	}
	if (!none) {
		_buttons[_selected]->setOver(false);
	}
	_selected = newSelected;
	_buttons[_selected]->setOver(true);
}

void Dropdown::buttonStateChanged(int oldState, ButtonStateChangeSource source) {
	if (source == ButtonByUser) {
		for (int32 i = 0, l = _buttons.size(); i < l; ++i) {
			if (_buttons[i]->getState() & Button::StateOver) {
				if (i != _selected) {
					_buttons[i]->setOver(false);
				}
			}
		}
	} else if (source == ButtonByHover) {
		bool found = false;
		for (int32 i = 0, l = _buttons.size(); i < l; ++i) {
			if (_buttons[i]->getState() & Button::StateOver) {
				found = true;
				if (i != _selected) {
					int32 sel = _selected;
					_selected = i;
					if (sel >= 0 && sel < _buttons.size()) {
						_buttons[sel]->setOver(false);
					}
				}
			}
		}
		if (!found) {
			_selected = -1;
		}
	}
}

void Dropdown::otherEnter() {
	_hideTimer.stop();
	showStart();
}

void Dropdown::otherLeave() {
	if (_a_appearance.animating()) {
		hideStart();
	} else {
		_hideTimer.start(0);
	}
}

void Dropdown::fastHide() {
	if (_a_appearance.animating()) {
		_a_appearance.stop();
	}
	a_opacity = anim::fvalue(0, 0);
	_hideTimer.stop();
	hide();
}

void Dropdown::adjustButtons() {
	for (Buttons::const_iterator i = _buttons.cbegin(), e = _buttons.cend(); i != e; ++i) {
		(*i)->setOpacity(a_opacity.current());
	}
}

void Dropdown::hideStart() {
	_hiding = true;
	a_opacity.start(0);
	_a_appearance.start();
}

void Dropdown::hideFinish() {
	emit hiding();
	hide();
	for (Buttons::const_iterator i = _buttons.cbegin(), e = _buttons.cend(); i != e; ++i) {
		(*i)->clearState();
	}
	_selected = -1;
}

void Dropdown::showStart() {
	if (!isHidden() && a_opacity.current() == 1) {
		return;
	}
	_selected = -1;
	_hiding = false;
	show();
	a_opacity.start(1);
	_a_appearance.start();
}

void Dropdown::step_appearance(float64 ms, bool timer) {
	float64 dt = ms / _st.duration;
	if (dt >= 1) {
		_a_appearance.stop();
		a_opacity.finish();
		if (_hiding) {
			hideFinish();
		}
	} else {
		a_opacity.update(dt, anim::linear);
	}
	adjustButtons();
	if (timer) update();
}

bool Dropdown::eventFilter(QObject *obj, QEvent *e) {
	if (e->type() == QEvent::Enter) {
		otherEnter();
	} else if (e->type() == QEvent::Leave) {
		otherLeave();
	} else if (e->type() == QEvent::MouseButtonPress && static_cast<QMouseEvent*>(e)->button() == Qt::LeftButton) {
		if (isHidden() || _hiding) {
			otherEnter();
		} else {
			otherLeave();
		}
	}
	return false;
}

DragArea::DragArea(QWidget *parent) : TWidget(parent)
, _hiding(false)
, _in(false)
, a_opacity(0)
, a_color(st::dragColor->c)
, _a_appearance(animation(this, &DragArea::step_appearance))
, _shadow(st::boxShadow) {
	setMouseTracking(true);
	setAcceptDrops(true);
}

void DragArea::mouseMoveEvent(QMouseEvent *e) {
	if (_hiding) return;

	bool newIn = QRect(st::dragPadding.left(), st::dragPadding.top(), width() - st::dragPadding.left() - st::dragPadding.right(), height() - st::dragPadding.top() - st::dragPadding.bottom()).contains(e->pos());
	if (newIn != _in) {
		_in = newIn;
		a_opacity.start(1);
		a_color.start((_in ? st::dragDropColor : st::dragColor)->c);
		_a_appearance.start();
	}
}

void DragArea::dragMoveEvent(QDragMoveEvent *e) {
	QRect r(st::dragPadding.left(), st::dragPadding.top(), width() - st::dragPadding.left() - st::dragPadding.right(), height() - st::dragPadding.top() - st::dragPadding.bottom());
	bool newIn = r.contains(e->pos());
	if (newIn != _in) {
		_in = newIn;
		a_opacity.start(1);
		a_color.start((_in ? st::dragDropColor : st::dragColor)->c);
		_a_appearance.start();
	}
	e->setDropAction(_in ? Qt::CopyAction : Qt::IgnoreAction);
	e->accept();
}

void DragArea::setText(const QString &text, const QString &subtext) {
	_text = text;
	_subtext = subtext;
	update();
}

void DragArea::paintEvent(QPaintEvent *e) {
	QPainter p(this);

	if (_a_appearance.animating()) {
		p.setOpacity(a_opacity.current());
	}

	QRect r(st::dragPadding.left(), st::dragPadding.top(), width() - st::dragPadding.left() - st::dragPadding.right(), height() - st::dragPadding.top() - st::dragPadding.bottom());

	// draw shadow
	_shadow.paint(p, r, st::boxShadowShift);

	p.fillRect(r, st::white->b);

	p.setPen(a_color.current());

	p.setFont(st::dragFont->f);
	p.drawText(QRect(0, (height() - st::dragHeight) / 2, width(), st::dragFont->height), _text, QTextOption(style::al_top));

	p.setFont(st::dragSubfont->f);
	p.drawText(QRect(0, (height() + st::dragHeight) / 2 - st::dragSubfont->height, width(), st::dragSubfont->height * 2), _subtext, QTextOption(style::al_top));
}

void DragArea::dragEnterEvent(QDragEnterEvent *e) {
	static_cast<HistoryWidget*>(parentWidget())->dragEnterEvent(e);
	e->setDropAction(Qt::IgnoreAction);
	e->accept();
}

void DragArea::dragLeaveEvent(QDragLeaveEvent *e) {
	static_cast<HistoryWidget*>(parentWidget())->dragLeaveEvent(e);
	_in = false;
	a_opacity.start(_hiding ? 0 : 1);
	a_color.start((_in ? st::dragDropColor : st::dragColor)->c);
	_a_appearance.start();
}

void DragArea::dropEvent(QDropEvent *e) {
	static_cast<HistoryWidget*>(parentWidget())->dropEvent(e);
	if (e->isAccepted()) {
		emit dropped(e->mimeData());
	}
}

void DragArea::otherEnter() {
	showStart();
}

void DragArea::otherLeave() {
	hideStart();
}

void DragArea::fastHide() {
	if (_a_appearance.animating()) {
		_a_appearance.stop();
	}
	a_opacity = anim::fvalue(0, 0);
	hide();
}

void DragArea::hideStart() {
	_hiding = true;
	_in = false;
	a_opacity.start(0);
	a_color.start((_in ? st::dragDropColor : st::dragColor)->c);
	_a_appearance.start();
}

void DragArea::hideFinish() {
	hide();
	_in = false;
	a_color = anim::cvalue(st::dragColor->c);
}

void DragArea::showStart() {
	_hiding = false;
	show();
	a_opacity.start(1);
	a_color.start((_in ? st::dragDropColor : st::dragColor)->c);
	_a_appearance.start();
}

void DragArea::step_appearance(float64 ms, bool timer) {
	float64 dt = ms / st::dropdownDef.duration;
	if (dt >= 1) {
		a_opacity.finish();
		a_color.finish();
		if (_hiding) {
			hideFinish();
		}
		_a_appearance.stop();
	} else {
		a_opacity.update(dt, anim::linear);
		a_color.update(dt, anim::linear);
	}
	if (timer) update();
}

namespace internal {

EmojiColorPicker::EmojiColorPicker() : TWidget()
, _ignoreShow(false)
, _a_selected(animation(this, &EmojiColorPicker::step_selected))
, _selected(-1)
, _pressedSel(-1)
, _hiding(false)
, a_opacity(0)
, _a_appearance(animation(this, &EmojiColorPicker::step_appearance))
, _shadow(st::dropdownDef.shadow) {
	memset(_variants, 0, sizeof(_variants));
	memset(_hovers, 0, sizeof(_hovers));

	setMouseTracking(true);
	setFocusPolicy(Qt::NoFocus);

	int32 w = st::emojiPanSize.width() * (EmojiColorsCount + 1) + 4 * st::emojiColorsPadding + st::emojiColorsSep + st::dropdownDef.shadow.pxWidth() * 2;
	int32 h = 2 * st::emojiColorsPadding + st::emojiPanSize.height() + st::dropdownDef.shadow.pxHeight() * 2;
	resize(w, h);

	_hideTimer.setSingleShot(true);
	connect(&_hideTimer, SIGNAL(timeout()), this, SLOT(hideStart()));
}

void EmojiColorPicker::showEmoji(uint32 code) {
	EmojiPtr e = emojiGet(code);
	if (!e || e == TwoSymbolEmoji || !e->color) {
		return;
	}
	_ignoreShow = false;

	_variants[0] = e;
	_variants[1] = emojiGet(e, 0xD83CDFFB);
	_variants[2] = emojiGet(e, 0xD83CDFFC);
	_variants[3] = emojiGet(e, 0xD83CDFFD);
	_variants[4] = emojiGet(e, 0xD83CDFFE);
	_variants[5] = emojiGet(e, 0xD83CDFFF);

	if (!_cache.isNull()) _cache = QPixmap();
	showStart();
}

void EmojiColorPicker::paintEvent(QPaintEvent *e) {
	Painter p(this);

	if (!_cache.isNull()) {
		p.setOpacity(a_opacity.current());
	}
	if (e->rect() != rect()) {
		p.setClipRect(e->rect());
	}

	int32 w = st::dropdownDef.shadow.pxWidth(), h = st::dropdownDef.shadow.pxHeight();
	QRect r = QRect(w, h, width() - 2 * w, height() - 2 * h);
	_shadow.paint(p, r, st::dropdownDef.shadowShift);

	if (_cache.isNull()) {
		p.fillRect(e->rect().intersected(r), st::white->b);

		int32 x = w + 2 * st::emojiColorsPadding + st::emojiPanSize.width();
		if (rtl()) x = width() - x - st::emojiColorsSep;
		p.fillRect(x, h + st::emojiColorsPadding, st::emojiColorsSep, r.height() - st::emojiColorsPadding * 2, st::emojiColorsSepColor->b);

		if (!_variants[0]) return;
		for (int i = 0; i < EmojiColorsCount + 1; ++i) {
			drawVariant(p, i);
		}
	} else {
		p.drawPixmap(r.left(), r.top(), _cache);
	}

}

void EmojiColorPicker::enterEvent(QEvent *e) {
	_hideTimer.stop();
	if (_hiding) showStart();
	TWidget::enterEvent(e);
}

void EmojiColorPicker::leaveEvent(QEvent *e) {
	TWidget::leaveEvent(e);
}

void EmojiColorPicker::mousePressEvent(QMouseEvent *e) {
	_lastMousePos = e->globalPos();
	updateSelected();
	_pressedSel = _selected;
}

void EmojiColorPicker::mouseReleaseEvent(QMouseEvent *e) {
	_lastMousePos = e ? e->globalPos() : QCursor::pos();
	int32 pressed = _pressedSel;
	_pressedSel = -1;

	updateSelected();
	if (_selected >= 0 && (pressed < 0 || _selected == pressed)) {
		emit emojiSelected(_variants[_selected]);
	}
	_ignoreShow = true;
	hideStart();
}

void EmojiColorPicker::mouseMoveEvent(QMouseEvent *e) {
	_lastMousePos = e ? e->globalPos() : QCursor::pos();
	updateSelected();
}

void EmojiColorPicker::step_appearance(float64 ms, bool timer) {
	if (_cache.isNull()) {
		_a_appearance.stop();
		return;
	}
	float64 dt = ms / st::dropdownDef.duration;
	if (dt >= 1) {
		a_opacity.finish();
		_cache = QPixmap();
		if (_hiding) {
			hide();
			emit hidden();
		} else {
			_lastMousePos = QCursor::pos();
			updateSelected();
		}
		_a_appearance.stop();
	} else {
		a_opacity.update(dt, anim::linear);
	}
	if (timer) update();
}

void EmojiColorPicker::step_selected(uint64 ms, bool timer) {
	QRegion toUpdate;
	for (EmojiAnimations::iterator i = _emojiAnimations.begin(); i != _emojiAnimations.end();) {
		int index = qAbs(i.key()) - 1;
		float64 dt = float64(ms - i.value()) / st::emojiPanDuration;
		if (dt >= 1) {
			_hovers[index] = (i.key() > 0) ? 1 : 0;
			i = _emojiAnimations.erase(i);
		} else {
			_hovers[index] = (i.key() > 0) ? dt : (1 - dt);
			++i;
		}
		toUpdate += QRect(st::dropdownDef.shadow.pxWidth() + st::emojiColorsPadding + index * st::emojiPanSize.width() + (index ? 2 * st::emojiColorsPadding + st::emojiColorsSep : 0), st::dropdownDef.shadow.pxHeight() + st::emojiColorsPadding, st::emojiPanSize.width(), st::emojiPanSize.height());
	}
	if (timer) rtlupdate(toUpdate.boundingRect());
	if (_emojiAnimations.isEmpty()) _a_selected.stop();
}

void EmojiColorPicker::hideStart(bool fast) {
	if (fast) {
		clearSelection(true);
		if (_a_appearance.animating()) _a_appearance.stop();
		if (_a_selected.animating()) _a_selected.stop();
		a_opacity = anim::fvalue(0);
		_cache = QPixmap();
		hide();
		emit hidden();
	} else {
		if (_cache.isNull()) {
			int32 w = st::dropdownDef.shadow.pxWidth(), h = st::dropdownDef.shadow.pxHeight();
			_cache = myGrab(this, QRect(w, h, width() - 2 * w, height() - 2 * h));
			clearSelection(true);
		}
		_hiding = true;
		a_opacity.start(0);
		_a_appearance.start();
	}
}

void EmojiColorPicker::showStart() {
	if (_ignoreShow) return;

	_hiding = false;
	if (!isHidden() && a_opacity.current() == 1) {
		if (_a_appearance.animating()) {
			_a_appearance.stop();
			_cache = QPixmap();
		}
		return;
	}
	if (_cache.isNull()) {
		int32 w = st::dropdownDef.shadow.pxWidth(), h = st::dropdownDef.shadow.pxHeight();
		_cache = myGrab(this, QRect(w, h, width() - 2 * w, height() - 2 * h));
		clearSelection(true);
	}
	show();
	a_opacity.start(1);
	_a_appearance.start();
}

void EmojiColorPicker::clearSelection(bool fast) {
	_pressedSel = -1;
	_lastMousePos = mapToGlobal(QPoint(-10, -10));
	if (fast) {
		_selected = -1;
		memset(_hovers, 0, sizeof(_hovers));
		_emojiAnimations.clear();
	} else {
		updateSelected();
	}
}

void EmojiColorPicker::updateSelected() {
	int32 selIndex = -1;
	QPoint p(mapFromGlobal(_lastMousePos));
	int32 sx = rtl() ? (width() - p.x()) : p.x(), y = p.y() - st::dropdownDef.shadow.pxHeight() - st::emojiColorsPadding;
	if (y >= 0 && y < st::emojiPanSize.height()) {
		int32 x = sx - st::dropdownDef.shadow.pxWidth() - st::emojiColorsPadding;
		if (x >= 0 && x < st::emojiPanSize.width()) {
			selIndex = 0;
		} else {
			x -= st::emojiPanSize.width() + 2 * st::emojiColorsPadding + st::emojiColorsSep;
			if (x >= 0 && x < st::emojiPanSize.width() * EmojiColorsCount) {
				selIndex = (x / st::emojiPanSize.width()) + 1;
			}
		}
	}

	bool startanim = false;
	if (selIndex != _selected) {
		if (_selected >= 0) {
			_emojiAnimations.remove(_selected + 1);
			if (_emojiAnimations.find(-_selected - 1) == _emojiAnimations.end()) {
				if (_emojiAnimations.isEmpty()) startanim = true;
				_emojiAnimations.insert(-_selected - 1, getms());
			}
		}
		_selected = selIndex;
		if (_selected >= 0) {
			_emojiAnimations.remove(-_selected - 1);
			if (_emojiAnimations.find(_selected + 1) == _emojiAnimations.end()) {
				if (_emojiAnimations.isEmpty()) startanim = true;
				_emojiAnimations.insert(_selected + 1, getms());
			}
		}
		setCursor((_selected >= 0) ? style::cur_pointer : style::cur_default);
	}
	if (startanim && !_a_selected.animating()) _a_selected.start();
}

void EmojiColorPicker::drawVariant(Painter &p, int variant) {
	float64 hover = _hovers[variant];

	QPoint w(st::dropdownDef.shadow.pxWidth() + st::emojiColorsPadding + variant * st::emojiPanSize.width() + (variant ? 2 * st::emojiColorsPadding + st::emojiColorsSep : 0), st::dropdownDef.shadow.pxHeight() + st::emojiColorsPadding);
	if (hover > 0) {
		p.setOpacity(hover);
		QPoint tl(w);
		if (rtl()) tl.setX(width() - tl.x() - st::emojiPanSize.width());
		App::roundRect(p, QRect(tl, st::emojiPanSize), st::emojiPanHover, StickerHoverCorners);
		p.setOpacity(1);
	}
	int esize = EmojiSizes[EIndex + 1];
	p.drawPixmapLeft(w.x() + (st::emojiPanSize.width() - (esize / cIntRetinaFactor())) / 2, w.y() + (st::emojiPanSize.height() - (esize / cIntRetinaFactor())) / 2, width(), App::emojiLarge(), QRect(_variants[variant]->x * esize, _variants[variant]->y * esize, esize, esize));
}

EmojiPanInner::EmojiPanInner() : TWidget()
, _maxHeight(int(st::emojiPanMaxHeight))
, _a_selected(animation(this, &EmojiPanInner::step_selected))
, _top(0)
, _selected(-1)
, _pressedSel(-1)
, _pickerSel(-1) {
	resize(st::emojiPanWidth - st::emojiScroll.width, countHeight());

	setMouseTracking(true);
	setFocusPolicy(Qt::NoFocus);
	setAttribute(Qt::WA_OpaquePaintEvent);

	_picker.hide();

	_esize = EmojiSizes[EIndex + 1];

	for (int32 i = 0; i < emojiTabCount; ++i) {
		_counts[i] = emojiPackCount(emojiTabAtIndex(i));
		_hovers[i] = QVector<float64>(_counts[i], 0);
	}

	_showPickerTimer.setSingleShot(true);
	connect(&_showPickerTimer, SIGNAL(timeout()), this, SLOT(onShowPicker()));
	connect(&_picker, SIGNAL(emojiSelected(EmojiPtr)), this, SLOT(onColorSelected(EmojiPtr)));
	connect(&_picker, SIGNAL(hidden()), this, SLOT(onPickerHidden()));
}

void EmojiPanInner::setMaxHeight(int32 h) {
	_maxHeight = h;
	resize(st::emojiPanWidth - st::emojiScroll.width, countHeight());
}

void EmojiPanInner::setScrollTop(int top) {
	_top = top;
}

int EmojiPanInner::countHeight() {
	int result = 0;
	for (int i = 0; i < emojiTabCount; ++i) {
		int cnt = emojiPackCount(emojiTabAtIndex(i)), rows = (cnt / EmojiPanPerRow) + ((cnt % EmojiPanPerRow) ? 1 : 0);
		result += st::emojiPanHeader + rows * st::emojiPanSize.height();
	}

	return result + st::emojiPanPadding;
}

void EmojiPanInner::paintEvent(QPaintEvent *e) {
	Painter p(this);
	QRect r = e ? e->rect() : rect();
	if (r != rect()) {
		p.setClipRect(r);
	}
	p.fillRect(r, st::white->b);

	int32 fromcol = floorclamp(r.x() - st::emojiPanPadding, st::emojiPanSize.width(), 0, EmojiPanPerRow);
	int32 tocol = ceilclamp(r.x() + r.width() - st::emojiPanPadding, st::emojiPanSize.width(), 0, EmojiPanPerRow);
	if (rtl()) {
		qSwap(fromcol, tocol);
		fromcol = EmojiPanPerRow - fromcol;
		tocol = EmojiPanPerRow - tocol;
	}

	int32 y, tilly = 0;
	for (int c = 0; c < emojiTabCount; ++c) {
		y = tilly;
		int32 size = _counts[c];
		int32 rows = (size / EmojiPanPerRow) + ((size % EmojiPanPerRow) ? 1 : 0);
		tilly = y + st::emojiPanHeader + (rows * st::emojiPanSize.height());
		if (r.top() >= tilly) continue;

		y += st::emojiPanHeader;
		if (_emojis[c].isEmpty()) {
			_emojis[c] = emojiPack(emojiTabAtIndex(c));
			if (emojiTabAtIndex(c) != dbietRecent) {
				for (EmojiPack::iterator i = _emojis[c].begin(), e = _emojis[c].end(); i != e; ++i) {
					if ((*i)->color) {
						EmojiColorVariants::const_iterator j = cEmojiVariants().constFind((*i)->code);
						if (j != cEmojiVariants().cend()) {
							EmojiPtr replace = emojiFromKey(j.value());
							if (replace) {
								if (replace != TwoSymbolEmoji && replace->code == (*i)->code && replace->code2 == (*i)->code2) {
									*i = replace;
								}
							}
						}
					}
				}
			}
		}

		int32 fromrow = floorclamp(r.y() - y, st::emojiPanSize.height(), 0, rows);
		int32 torow = ceilclamp(r.y() + r.height() - y, st::emojiPanSize.height(), 0, rows);
		for (int32 i = fromrow; i < torow; ++i) {
			for (int32 j = fromcol; j < tocol; ++j) {
				int32 index = i * EmojiPanPerRow + j;
				if (index >= size) break;

				float64 hover = (!_picker.isHidden() && c * MatrixRowShift + index == _pickerSel) ? 1 : _hovers[c][index];

				QPoint w(st::emojiPanPadding + j * st::emojiPanSize.width(), y + i * st::emojiPanSize.height());
				if (hover > 0) {
					p.setOpacity(hover);
					QPoint tl(w);
					if (rtl()) tl.setX(width() - tl.x() - st::emojiPanSize.width());
					App::roundRect(p, QRect(tl, st::emojiPanSize), st::emojiPanHover, StickerHoverCorners);
					p.setOpacity(1);
				}
				p.drawPixmapLeft(w.x() + (st::emojiPanSize.width() - (_esize / cIntRetinaFactor())) / 2, w.y() + (st::emojiPanSize.height() - (_esize / cIntRetinaFactor())) / 2, width(), App::emojiLarge(), QRect(_emojis[c][index]->x * _esize, _emojis[c][index]->y * _esize, _esize, _esize));
			}
		}
	}
}

bool EmojiPanInner::checkPickerHide() {
	if (!_picker.isHidden() && _selected == _pickerSel) {
		_picker.hideStart();
		_pickerSel = -1;
		updateSelected();
		return true;
	}
	return false;
}

void EmojiPanInner::mousePressEvent(QMouseEvent *e) {
	_lastMousePos = e->globalPos();
	updateSelected();
	if (checkPickerHide()) {
		return;
	}
	_pressedSel = _selected;

	if (_selected >= 0) {
		int tab = (_selected / MatrixRowShift), sel = _selected % MatrixRowShift;
		if (tab < emojiTabCount && sel < _emojis[tab].size() && _emojis[tab][sel]->color) {
			_pickerSel = _selected;
			setCursor(style::cur_default);
			if (cEmojiVariants().constFind(_emojis[tab][sel]->code) == cEmojiVariants().cend()) {
				onShowPicker();
			} else {
				_showPickerTimer.start(500);
			}
		}
	}
}

void EmojiPanInner::mouseReleaseEvent(QMouseEvent *e) {
	int32 pressed = _pressedSel;
	_pressedSel = -1;

	_lastMousePos = e->globalPos();
	if (!_picker.isHidden()) {
		if (_picker.rect().contains(_picker.mapFromGlobal(_lastMousePos))) {
			return _picker.mouseReleaseEvent(0);
		} else if (_pickerSel >= 0) {
			int tab = (_pickerSel / MatrixRowShift), sel = _pickerSel % MatrixRowShift;
			if (tab < emojiTabCount && sel < _emojis[tab].size() && _emojis[tab][sel]->color) {
				if (cEmojiVariants().constFind(_emojis[tab][sel]->code) != cEmojiVariants().cend()) {
					_picker.hideStart();
					_pickerSel = -1;
				}
			}
		}
	}
	updateSelected();

	if (_showPickerTimer.isActive()) {
		_showPickerTimer.stop();
		_pickerSel = -1;
		_picker.hide();
	}

	if (_selected < 0 || _selected != pressed) return;

	if (_selected >= emojiTabCount * MatrixRowShift) {
		return;
	}

	int tab = (_selected / MatrixRowShift), sel = _selected % MatrixRowShift;
	if (sel < _emojis[tab].size()) {
		EmojiPtr emoji(_emojis[tab][sel]);
		if (emoji->color && !_picker.isHidden()) return;

		selectEmoji(emoji);
	}
}

void EmojiPanInner::selectEmoji(EmojiPtr emoji) {
	RecentEmojiPack &recent(cGetRecentEmojis());
	RecentEmojiPack::iterator i = recent.begin(), e = recent.end();
	for (; i != e; ++i) {
		if (i->first == emoji) {
			++i->second;
			if (i->second > 0x8000) {
				for (RecentEmojiPack::iterator j = recent.begin(); j != e; ++j) {
					if (j->second > 1) {
						j->second /= 2;
					} else {
						j->second = 1;
					}
				}
			}
			for (; i != recent.begin(); --i) {
				if ((i - 1)->second > i->second) {
					break;
				}
				qSwap(*i, *(i - 1));
			}
			break;
		}
	}
	if (i == e) {
		while (recent.size() >= EmojiPanPerRow * EmojiPanRowsPerPage) recent.pop_back();
		recent.push_back(qMakePair(emoji, 1));
		for (i = recent.end() - 1; i != recent.begin(); --i) {
			if ((i - 1)->second > i->second) {
				break;
			}
			qSwap(*i, *(i - 1));
		}
	}
	emit saveConfigDelayed(SaveRecentEmojisTimeout);

	emit selected(emoji);
}

void EmojiPanInner::onShowPicker() {
	if (_pickerSel < 0) return;

	int tab = (_pickerSel / MatrixRowShift), sel = _pickerSel % MatrixRowShift;
	if (tab < emojiTabCount && sel < _emojis[tab].size() && _emojis[tab][sel]->color) {
		int32 y = 0;
		for (int c = 0; c <= tab; ++c) {
			int32 size = (c == tab) ? (sel - (sel % EmojiPanPerRow)) : _counts[c], rows = (size / EmojiPanPerRow) + ((size % EmojiPanPerRow) ? 1 : 0);
			y += st::emojiPanHeader + (rows * st::emojiPanSize.height());
		}
		y -= _picker.height() - st::msgRadius + _top;
		if (y < 0) {
			y += _picker.height() - st::msgRadius + st::emojiPanSize.height() - st::msgRadius;
		}
		int xmax = width() - _picker.width();
		float64 coef = float64(sel % EmojiPanPerRow) / float64(EmojiPanPerRow - 1);
		if (rtl()) coef = 1. - coef;
		_picker.move(qRound(xmax * coef), y);

		_picker.showEmoji(_emojis[tab][sel]->code);
		emit disableScroll(true);
	}
}

void EmojiPanInner::onPickerHidden() {
	_pickerSel = -1;
	update();
	emit disableScroll(false);

	_lastMousePos = QCursor::pos();
	updateSelected();
}

QRect EmojiPanInner::emojiRect(int tab, int sel) {
	int x = 0, y = 0;
	for (int i = 0; i < emojiTabCount; ++i) {
		if (i == tab) {
			int rows = (sel / EmojiPanPerRow);
			y += st::emojiPanHeader + rows * st::emojiPanSize.height();
			x = st::emojiPanPadding + ((sel % EmojiPanPerRow) * st::emojiPanSize.width());
			break;
		} else {
			int cnt = _counts[i];
			int rows = (cnt / EmojiPanPerRow) + ((cnt % EmojiPanPerRow) ? 1 : 0);
			y += st::emojiPanHeader + rows * st::emojiPanSize.height();
		}
	}
	return QRect(x, y, st::emojiPanSize.width(), st::emojiPanSize.height());
}

void EmojiPanInner::onColorSelected(EmojiPtr emoji) {
	if (emoji->color) {
		cRefEmojiVariants().insert(emoji->code, emojiKey(emoji));
	}
	if (_pickerSel >= 0) {
		int tab = (_pickerSel / MatrixRowShift), sel = _pickerSel % MatrixRowShift;
		if (tab >= 0 && tab < emojiTabCount) {
			_emojis[tab][sel] = emoji;
			rtlupdate(emojiRect(tab, sel));
		}
	}
	selectEmoji(emoji);
	_picker.hideStart();
}

void EmojiPanInner::mouseMoveEvent(QMouseEvent *e) {
	_lastMousePos = e->globalPos();
	if (!_picker.isHidden()) {
		if (_picker.rect().contains(_picker.mapFromGlobal(_lastMousePos))) {
			return _picker.mouseMoveEvent(0);
		} else {
			_picker.clearSelection();
		}
	}
	updateSelected();
}

void EmojiPanInner::leaveEvent(QEvent *e) {
	clearSelection();
}

void EmojiPanInner::leaveToChildEvent(QEvent *e, QWidget *child) {
	clearSelection();
}

void EmojiPanInner::enterFromChildEvent(QEvent *e, QWidget *child) {
	_lastMousePos = QCursor::pos();
	updateSelected();
}

void EmojiPanInner::clearSelection(bool fast) {
	_lastMousePos = mapToGlobal(QPoint(-10, -10));
	if (fast) {
		for (Animations::const_iterator i = _animations.cbegin(); i != _animations.cend(); ++i) {
			int index = qAbs(i.key()) - 1, tab = (index / MatrixRowShift), sel = index % MatrixRowShift;
			_hovers[tab][sel] = 0;
		}
		_animations.clear();
		if (_selected >= 0) {
			int index = qAbs(_selected), tab = (index / MatrixRowShift), sel = index % MatrixRowShift;
			_hovers[tab][sel] = 0;
		}
		if (_pressedSel >= 0) {
			int index = qAbs(_pressedSel), tab = (index / MatrixRowShift), sel = index % MatrixRowShift;
			_hovers[tab][sel] = 0;
		}
		_selected = _pressedSel = -1;
		_a_selected.stop();
	} else {
		updateSelected();
	}
}

DBIEmojiTab EmojiPanInner::currentTab(int yOffset) const {
	int y, ytill = 0;
	for (int c = 0; c < emojiTabCount; ++c) {
		int cnt = _counts[c];
		y = ytill;
		ytill = y + st::emojiPanHeader + ((cnt / EmojiPanPerRow) + ((cnt % EmojiPanPerRow) ? 1 : 0)) * st::emojiPanSize.height();
		if (yOffset < ytill) {
			return emojiTabAtIndex(c);
		}
	}
	return emojiTabAtIndex(emojiTabCount - 1);
}

void EmojiPanInner::hideFinish() {
	if (!_picker.isHidden()) {
		_picker.hideStart(true);
		_pickerSel = -1;
		clearSelection(true);
	}
}

void EmojiPanInner::refreshRecent() {
	clearSelection(true);
	_counts[0] = emojiPackCount(dbietRecent);
	if (_hovers[0].size() != _counts[0]) _hovers[0] = QVector<float64>(_counts[0], 0);
	_emojis[0] = emojiPack(dbietRecent);
	int32 h = countHeight();
	if (h != height()) {
		resize(width(), h);
		emit needRefreshPanels();
	}
}

void EmojiPanInner::fillPanels(QVector<EmojiPanel*> &panels) {
	if (_picker.parentWidget() != parentWidget()) {
		_picker.setParent(parentWidget());
	}
	for (int32 i = 0; i < panels.size(); ++i) {
		panels.at(i)->hide();
		panels.at(i)->deleteLater();
	}
	panels.clear();

	int y = 0;
	panels.reserve(emojiTabCount);
	for (int c = 0; c < emojiTabCount; ++c) {
		panels.push_back(new EmojiPanel(parentWidget(), lang(LangKey(lng_emoji_category0 + c)), Stickers::NoneSetId, true, y));
		connect(panels.back(), SIGNAL(mousePressed()), this, SLOT(checkPickerHide()));
		int cnt = _counts[c], rows = (cnt / EmojiPanPerRow) + ((cnt % EmojiPanPerRow) ? 1 : 0);
		panels.back()->show();
		y += st::emojiPanHeader + rows * st::emojiPanSize.height();
	}
	_picker.raise();
}

void EmojiPanInner::refreshPanels(QVector<EmojiPanel*> &panels) {
	if (panels.size() != emojiTabCount) return fillPanels(panels);

	int32 y = 0;
	for (int c = 0; c < emojiTabCount; ++c) {
		panels.at(c)->setWantedY(y);
		int cnt = _counts[c], rows = (cnt / EmojiPanPerRow) + ((cnt % EmojiPanPerRow) ? 1 : 0);
		y += st::emojiPanHeader + rows * st::emojiPanSize.height();
	}
}

void EmojiPanInner::updateSelected() {
	if (_pressedSel >= 0 || _pickerSel >= 0) return;

	int32 selIndex = -1;
	QPoint p(mapFromGlobal(_lastMousePos));
	int y, ytill = 0, sx = (rtl() ? width() - p.x() : p.x()) - st::emojiPanPadding;
	for (int c = 0; c < emojiTabCount; ++c) {
		int cnt = _counts[c];
		y = ytill;
		ytill = y + st::emojiPanHeader + ((cnt / EmojiPanPerRow) + ((cnt % EmojiPanPerRow) ? 1 : 0)) * st::emojiPanSize.height();
		if (p.y() >= y && p.y() < ytill) {
			y += st::emojiPanHeader;
			if (p.y() >= y && sx >= 0 && sx < EmojiPanPerRow * st::emojiPanSize.width()) {
				selIndex = qFloor((p.y() - y) / st::emojiPanSize.height()) * EmojiPanPerRow + qFloor(sx / st::emojiPanSize.width());
				if (selIndex >= _emojis[c].size()) {
					selIndex = -1;
				} else {
					selIndex += c * MatrixRowShift;
				}
			}
			break;
		}
	}

	bool startanim = false;
	int oldSel = _selected, newSel = selIndex;

	if (newSel != oldSel) {
		if (oldSel >= 0) {
			_animations.remove(oldSel + 1);
			if (_animations.find(-oldSel - 1) == _animations.end()) {
				if (_animations.isEmpty()) startanim = true;
				_animations.insert(-oldSel - 1, getms());
			}
		}
		if (newSel >= 0) {
			_animations.remove(-newSel - 1);
			if (_animations.find(newSel + 1) == _animations.end()) {
				if (_animations.isEmpty()) startanim = true;
				_animations.insert(newSel + 1, getms());
			}
		}
		setCursor((newSel >= 0) ? style::cur_pointer : style::cur_default);
		if (newSel >= 0 && !_picker.isHidden()) {
			if (newSel != _pickerSel) {
				_picker.hideStart();
			} else {
				_picker.showStart();
			}
		}
	}

	_selected = selIndex;
	if (startanim && !_a_selected.animating()) _a_selected.start();
}

void EmojiPanInner::step_selected(uint64 ms, bool timer) {
	QRegion toUpdate;
	for (Animations::iterator i = _animations.begin(); i != _animations.end();) {
		int index = qAbs(i.key()) - 1, tab = (index / MatrixRowShift), sel = index % MatrixRowShift;
		float64 dt = float64(ms - i.value()) / st::emojiPanDuration;
		if (dt >= 1) {
			_hovers[tab][sel] = (i.key() > 0) ? 1 : 0;
			i = _animations.erase(i);
		} else {
			_hovers[tab][sel] = (i.key() > 0) ? dt : (1 - dt);
			++i;
		}
		toUpdate += emojiRect(tab, sel);
	}
	if (timer) rtlupdate(toUpdate.boundingRect());
	if (_animations.isEmpty()) _a_selected.stop();
}

void InlineCacheEntry::clearResults() {
	for_const (const InlineBots::Result *result, results) {
		delete result;
	}
	results.clear();
}

void EmojiPanInner::showEmojiPack(DBIEmojiTab packIndex) {
	clearSelection(true);

	refreshRecent();

	int32 y = 0;
	for (int c = 0; c < emojiTabCount; ++c) {
		if (emojiTabAtIndex(c) == packIndex) break;
		int rows = (_counts[c] / EmojiPanPerRow) + ((_counts[c] % EmojiPanPerRow) ? 1 : 0);
		y += st::emojiPanHeader + rows * st::emojiPanSize.height();
	}

	emit scrollToY(y);

	_lastMousePos = QCursor::pos();

	update();
}

StickerPanInner::StickerPanInner() : TWidget()
, _a_selected(animation(this, &StickerPanInner::step_selected))
, _top(0)
, _showingSavedGifs(cShowingSavedGifs())
, _showingInlineItems(_showingSavedGifs)
, _setGifCommand(false)
, _lastScrolled(0)
, _inlineWithThumb(false)
, _selected(-1)
, _pressedSel(-1)
, _settings(this, lang(lng_stickers_you_have))
, _previewShown(false) {
	setMaxHeight(st::emojiPanMaxHeight);

	setMouseTracking(true);
	setFocusPolicy(Qt::NoFocus);
	setAttribute(Qt::WA_OpaquePaintEvent);

	connect(App::wnd(), SIGNAL(imageLoaded()), this, SLOT(update()));
	connect(&_settings, SIGNAL(clicked()), this, SLOT(onSettings()));

	_previewTimer.setSingleShot(true);
	connect(&_previewTimer, SIGNAL(timeout()), this, SLOT(onPreview()));

	_updateInlineItems.setSingleShot(true);
	connect(&_updateInlineItems, SIGNAL(timeout()), this, SLOT(onUpdateInlineItems()));
}

void StickerPanInner::setMaxHeight(int32 h) {
	_maxHeight = h;
	resize(st::emojiPanWidth - st::emojiScroll.width, countHeight());
	_settings.moveToLeft((st::emojiPanWidth - _settings.width()) / 2, height() / 3);
}

void StickerPanInner::setScrollTop(int top) {
	if (top == _top) return;

	_lastScrolled = getms();
	_top = top;
}

int32 StickerPanInner::countHeight(bool plain) {
	int result = 0, minLastH = plain ? 0 : (_maxHeight - st::stickerPanPadding);
	if (_showingInlineItems) {
		result = st::emojiPanHeader;
		if (_switchPmButton) {
			result += _switchPmButton->height() + st::inlineResultsSkip;
		}
		for (int i = 0, l = _inlineRows.count(); i < l; ++i) {
			result += _inlineRows.at(i).height;
		}
	} else {
		for (int i = 0; i < _sets.size(); ++i) {
			int cnt = _sets.at(i).pack.size(), rows = (cnt / StickerPanPerRow) + ((cnt % StickerPanPerRow) ? 1 : 0);
			int h = st::emojiPanHeader + rows * st::stickerPanSize.height();
			if (i == _sets.size() - 1 && h < minLastH) h = minLastH;
			result += h;
		}
	}
	return qMax(minLastH, result) + st::stickerPanPadding;
}

StickerPanInner::~StickerPanInner() {
	clearInlineRows(true);
	deleteUnusedGifLayouts();
	deleteUnusedInlineLayouts();
}

QRect StickerPanInner::stickerRect(int tab, int sel) {
	int x = 0, y = 0;
	for (int i = 0; i < _sets.size(); ++i) {
		if (i == tab) {
			int rows = (((sel >= _sets.at(i).pack.size()) ? (sel - _sets.at(i).pack.size()) : sel) / StickerPanPerRow);
			y += st::emojiPanHeader + rows * st::stickerPanSize.height();
			x = st::stickerPanPadding + ((sel % StickerPanPerRow) * st::stickerPanSize.width());
			break;
		} else {
			int cnt = _sets.at(i).pack.size();
			int rows = (cnt / StickerPanPerRow) + ((cnt % StickerPanPerRow) ? 1 : 0);
			y += st::emojiPanHeader + rows * st::stickerPanSize.height();
		}
	}
	return QRect(x, y, st::stickerPanSize.width(), st::stickerPanSize.height());
}

void StickerPanInner::paintEvent(QPaintEvent *e) {
	Painter p(this);
	QRect r = e ? e->rect() : rect();
	if (r != rect()) {
		p.setClipRect(r);
	}
	p.fillRect(r, st::white);

	if (_showingInlineItems) {
		paintInlineItems(p, r);
	} else {
		paintStickers(p, r);
	}
}

void StickerPanInner::paintInlineItems(Painter &p, const QRect &r) {
	if (_inlineRows.isEmpty()) {
		p.setFont(st::normalFont);
		p.setPen(st::noContactsColor);
		p.drawText(QRect(0, 0, width(), (height() / 3) * 2 + st::normalFont->height), lang(lng_inline_bot_no_results), style::al_center);
		return;
	}
	InlineBots::Layout::PaintContext context(getms(), false, Ui::isLayerShown() || Ui::isMediaViewShown() || _previewShown, false);

	int top = st::emojiPanHeader;
	if (_switchPmButton) {
		top += _switchPmButton->height() + st::inlineResultsSkip;
	}

	int fromx = rtl() ? (width() - r.x() - r.width()) : r.x(), tox = rtl() ? (width() - r.x()) : (r.x() + r.width());
	for (int row = 0, rows = _inlineRows.size(); row < rows; ++row) {
		const InlineRow &inlineRow(_inlineRows.at(row));
		if (top >= r.top() + r.height()) break;
		if (top + inlineRow.height > r.top()) {
			int left = st::inlineResultsLeft;
			if (row == rows - 1) context.lastRow = true;
			for (int col = 0, cols = inlineRow.items.size(); col < cols; ++col) {
				if (left >= tox) break;

				const InlineItem *item = inlineRow.items.at(col);
				int w = item->width();
				if (left + w > fromx) {
					p.translate(left, top);
					item->paint(p, r.translated(-left, -top), &context);
					p.translate(-left, -top);
				}
				left += w;
				if (item->hasRightSkip()) {
					left += st::inlineResultsSkip;
				}
			}
		}
		top += inlineRow.height;
	}
}

void StickerPanInner::paintStickers(Painter &p, const QRect &r) {
	int32 fromcol = floorclamp(r.x() - st::stickerPanPadding, st::stickerPanSize.width(), 0, StickerPanPerRow);
	int32 tocol = ceilclamp(r.x() + r.width() - st::stickerPanPadding, st::stickerPanSize.width(), 0, StickerPanPerRow);
	if (rtl()) {
		qSwap(fromcol, tocol);
		fromcol = StickerPanPerRow - fromcol;
		tocol = StickerPanPerRow - tocol;
	}

	int32 y, tilly = 0;
	for (int c = 0, l = _sets.size(); c < l; ++c) {
		y = tilly;
		int32 size = _sets.at(c).pack.size();
		int32 rows = (size / StickerPanPerRow) + ((size % StickerPanPerRow) ? 1 : 0);
		tilly = y + st::emojiPanHeader + (rows * st::stickerPanSize.height());
		if (r.top() >= tilly) continue;

		bool special = (_sets[c].flags & MTPDstickerSet::Flag::f_official);
		y += st::emojiPanHeader;

		int32 fromrow = floorclamp(r.y() - y, st::stickerPanSize.height(), 0, rows);
		int32 torow = ceilclamp(r.y() + r.height() - y, st::stickerPanSize.height(), 0, rows);
		for (int32 i = fromrow; i < torow; ++i) {
			for (int32 j = fromcol; j < tocol; ++j) {
				int32 index = i * StickerPanPerRow + j;
				if (index >= size) break;

				float64 hover = _sets[c].hovers[index];

				DocumentData *sticker = _sets[c].pack[index];
				if (!sticker->sticker()) continue;

				QPoint pos(st::stickerPanPadding + j * st::stickerPanSize.width(), y + i * st::stickerPanSize.height());
				if (hover > 0) {
					p.setOpacity(hover);
					QPoint tl(pos);
					if (rtl()) tl.setX(width() - tl.x() - st::stickerPanSize.width());
					App::roundRect(p, QRect(tl, st::stickerPanSize), st::emojiPanHover, StickerHoverCorners);
					p.setOpacity(1);
				}

				bool goodThumb = !sticker->thumb->isNull() && ((sticker->thumb->width() >= 128) || (sticker->thumb->height() >= 128));
				if (goodThumb) {
					sticker->thumb->load();
				} else {
					sticker->checkSticker();
				}

				float64 coef = qMin((st::stickerPanSize.width() - st::msgRadius * 2) / float64(sticker->dimensions.width()), (st::stickerPanSize.height() - st::msgRadius * 2) / float64(sticker->dimensions.height()));
				if (coef > 1) coef = 1;
				int32 w = qRound(coef * sticker->dimensions.width()), h = qRound(coef * sticker->dimensions.height());
				if (w < 1) w = 1;
				if (h < 1) h = 1;
				QPoint ppos = pos + QPoint((st::stickerPanSize.width() - w) / 2, (st::stickerPanSize.height() - h) / 2);
				if (goodThumb) {
					p.drawPixmapLeft(ppos, width(), sticker->thumb->pix(w, h));
				} else if (!sticker->sticker()->img->isNull()) {
					p.drawPixmapLeft(ppos, width(), sticker->sticker()->img->pix(w, h));
				}

				if (hover > 0 && _sets[c].id == Stickers::RecentSetId && _custom.at(index)) {
					float64 xHover = _sets[c].hovers[_sets[c].pack.size() + index];

					QPoint xPos = pos + QPoint(st::stickerPanSize.width() - st::stickerPanDelete.pxWidth(), 0);
					p.setOpacity(hover * (xHover + (1 - xHover) * st::stickerPanDeleteOpacity));
					p.drawSpriteLeft(xPos, width(), st::stickerPanDelete);
					p.setOpacity(1);
				}
			}
		}
	}
}

void StickerPanInner::mousePressEvent(QMouseEvent *e) {
	_lastMousePos = e->globalPos();
	updateSelected();

	_pressedSel = _selected;
	ClickHandler::pressed();
	_previewTimer.start(QApplication::startDragTime());
}

void StickerPanInner::mouseReleaseEvent(QMouseEvent *e) {
	_previewTimer.stop();

	int32 pressed = _pressedSel;
	_pressedSel = -1;

	ClickHandlerPtr activated = ClickHandler::unpressed();

	_lastMousePos = e->globalPos();
	updateSelected();

	if (_previewShown) {
		_previewShown = false;
		return;
	}

	if (_selected < 0 || _selected != pressed || (_showingInlineItems && !activated)) return;
	if (_showingInlineItems) {
		if (!dynamic_cast<InlineBots::Layout::SendClickHandler*>(activated.data())) {
			App::activateClickHandler(activated, e->button());
			return;
		}
		int row = _selected / MatrixRowShift, col = _selected % MatrixRowShift;
		if (row >= _inlineRows.size() || col >= _inlineRows.at(row).items.size()) {
			return;
		}

		InlineItem *item = _inlineRows.at(row).items.at(col);
		if (PhotoData *photo = item->getPhoto()) {
			if (photo->medium->loaded() || photo->thumb->loaded()) {
				emit selected(photo);
			} else if (!photo->medium->loading()) {
				photo->thumb->loadEvenCancelled();
				photo->medium->loadEvenCancelled();
			}
		} else if (DocumentData *document = item->getDocument()) {
			if (document->loaded()) {
				emit selected(document);
			} else if (document->loading()) {
				document->cancel();
			} else {
				DocumentOpenClickHandler::doOpen(document, ActionOnLoadNone);
			}
		} else if (InlineResult *inlineResult = item->getResult()) {
			if (inlineResult->onChoose(item)) {
				emit selected(inlineResult, _inlineBot);
			}
		}
		return;
	}
	if (_selected >= MatrixRowShift * _sets.size()) {
		return;
	}

	int tab = (_selected / MatrixRowShift), sel = _selected % MatrixRowShift;
	if (_sets[tab].id == Stickers::RecentSetId && sel >= _sets[tab].pack.size() && sel < _sets[tab].pack.size() * 2 && _custom.at(sel - _sets[tab].pack.size())) {
		clearSelection(true);
		bool refresh = false;
		DocumentData *sticker = _sets[tab].pack.at(sel - _sets[tab].pack.size());
		RecentStickerPack &recent(cGetRecentStickers());
		for (int32 i = 0, l = recent.size(); i < l; ++i) {
			if (recent.at(i).first == sticker) {
				recent.removeAt(i);
				Local::writeUserSettings();
				refresh = true;
				break;
			}
		}
		Stickers::Sets &sets(Global::RefStickerSets());
		auto it = sets.find(Stickers::CustomSetId);
		if (it != sets.cend()) {
			for (int32 i = 0, l = it->stickers.size(); i < l; ++i) {
				if (it->stickers.at(i) == sticker) {
					it->stickers.removeAt(i);
					if (it->stickers.isEmpty()) {
						sets.erase(it);
					}
					Local::writeStickers();
					refresh = true;
					break;
				}
			}
		}
		if (refresh) {
			refreshRecentStickers();
			updateSelected();
			update();
		}
		return;
	}
	if (sel < _sets[tab].pack.size()) {
		emit selected(_sets[tab].pack[sel]);
	}
}

void StickerPanInner::mouseMoveEvent(QMouseEvent *e) {
	_lastMousePos = e->globalPos();
	updateSelected();
}

void StickerPanInner::leaveEvent(QEvent *e) {
	clearSelection();
}

void StickerPanInner::leaveToChildEvent(QEvent *e, QWidget *child) {
	clearSelection();
}

void StickerPanInner::enterFromChildEvent(QEvent *e, QWidget *child) {
	_lastMousePos = QCursor::pos();
	updateSelected();
}

bool StickerPanInner::showSectionIcons() const {
	return !inlineResultsShown();
}

void StickerPanInner::clearSelection(bool fast) {
	_lastMousePos = mapToGlobal(QPoint(-10, -10));
	if (fast) {
		if (_showingInlineItems) {
			if (_selected >= 0) {
				int32 srow = _selected / MatrixRowShift, scol = _selected % MatrixRowShift;
				t_assert(srow >= 0 && srow < _inlineRows.size() && scol >= 0 && scol < _inlineRows.at(srow).items.size());
				ClickHandler::clearActive(_inlineRows.at(srow).items.at(scol));
				setCursor(style::cur_default);
			}
			_selected = _pressedSel = -1;
			return;
		}
		for (Animations::const_iterator i = _animations.cbegin(); i != _animations.cend(); ++i) {
			int index = qAbs(i.key()) - 1, tab = (index / MatrixRowShift), sel = index % MatrixRowShift;
			_sets[tab].hovers[sel] = 0;
		}
		_animations.clear();
		if (_selected >= 0) {
			int index = qAbs(_selected), tab = (index / MatrixRowShift), sel = index % MatrixRowShift;
			if (index >= 0 && tab < _sets.size() && _sets[tab].id == Stickers::RecentSetId && sel >= tab * MatrixRowShift + _sets[tab].pack.size()) {
				_sets[tab].hovers[sel] = 0;
				sel -= _sets[tab].pack.size();
			}
			_sets[tab].hovers[sel] = 0;
		}
		if (_pressedSel >= 0) {
			int index = qAbs(_pressedSel), tab = (index / MatrixRowShift), sel = index % MatrixRowShift;
			if (index >= 0 && tab < _sets.size() && _sets[tab].id == Stickers::RecentSetId && sel >= tab * MatrixRowShift + _sets[tab].pack.size()) {
				_sets[tab].hovers[sel] = 0;
				sel -= _sets[tab].pack.size();
			}
			_sets[tab].hovers[sel] = 0;
		}
		_selected = _pressedSel = -1;
		_a_selected.stop();
	} else {
		updateSelected();
	}
}

void StickerPanInner::hideFinish(bool completely) {
	if (completely) {
		auto itemForget = [](const InlineItem *item) {
			if (DocumentData *document = item->getDocument()) {
				document->forget();
			}
			if (PhotoData *photo = item->getPhoto()) {
				photo->forget();
			}
			if (InlineResult *result = item->getResult()) {
				result->forget();
			}
		};
		clearInlineRows(false);
		for_const (InlineItem *item, _gifLayouts) {
			itemForget(item);
		}
		for_const (InlineItem *item, _inlineLayouts) {
			itemForget(item);
		}
	}
	if (_setGifCommand && _showingSavedGifs) {
		App::insertBotCommand(qsl(""), true);
	}
	_setGifCommand = false;
}

void StickerPanInner::refreshStickers() {
	clearSelection(true);

	const Stickers::Sets &sets(Global::StickerSets());
	_sets.clear(); _sets.reserve(sets.size() + 1);

	refreshRecentStickers(false);
	for (auto i = Global::StickerSetsOrder().cbegin(), e = Global::StickerSetsOrder().cend(); i != e; ++i) {
		appendSet(*i);
	}

	if (_showingInlineItems) {
		_settings.hide();
	} else {
		int32 h = countHeight();
		if (h != height()) resize(width(), h);

		_settings.setVisible(_sets.isEmpty());
	}


	emit refreshIcons();

	updateSelected();
}

bool StickerPanInner::inlineRowsAddItem(DocumentData *savedGif, InlineResult *result, InlineRow &row, int32 &sumWidth) {
	InlineItem *layout = nullptr;
	if (savedGif) {
		layout = layoutPrepareSavedGif(savedGif, (_inlineRows.size() * MatrixRowShift) + row.items.size());
	} else if (result) {
		layout = layoutPrepareInlineResult(result, (_inlineRows.size() * MatrixRowShift) + row.items.size());
	}
	if (!layout) return false;

	layout->preload();
	if (inlineRowFinalize(row, sumWidth, layout->isFullLine())) {
		layout->setPosition(_inlineRows.size() * MatrixRowShift);
	}

	sumWidth += layout->maxWidth();
	if (!row.items.isEmpty() && row.items.back()->hasRightSkip()) {
		sumWidth += st::inlineResultsSkip;
	}

	row.items.push_back(layout);
	return true;
}

bool StickerPanInner::inlineRowFinalize(InlineRow &row, int32 &sumWidth, bool force) {
	if (row.items.isEmpty()) return false;

	bool full = (row.items.size() >= InlineItemsMaxPerRow);
	bool big = (sumWidth >= st::emojiPanWidth - st::emojiScroll.width - st::inlineResultsLeft);
	if (full || big || force) {
		_inlineRows.push_back(layoutInlineRow(row, (full || big) ? sumWidth : 0));
		row = InlineRow();
		row.items.reserve(InlineItemsMaxPerRow);
		sumWidth = 0;
		return true;
	}
	return false;
}

void StickerPanInner::refreshSavedGifs() {
	if (_showingSavedGifs) {
		_settings.hide();
		clearInlineRows(false);
		if (_showingInlineItems) {
			const SavedGifs &saved(cSavedGifs());
			if (saved.isEmpty()) {
				showStickerSet(Stickers::RecentSetId);
				return;
			} else {
				_inlineRows.reserve(saved.size());
				InlineRow row;
				row.items.reserve(InlineItemsMaxPerRow);
				int32 sumWidth = 0;
				for (SavedGifs::const_iterator i = saved.cbegin(), e = saved.cend(); i != e; ++i) {
					inlineRowsAddItem(*i, 0, row, sumWidth);
				}
				inlineRowFinalize(row, sumWidth, true);
			}
			deleteUnusedGifLayouts();

			int32 h = countHeight();
			if (h != height()) resize(width(), h);

			update();
		}
	}
	emit refreshIcons();

	updateSelected();
}

void StickerPanInner::inlineBotChanged() {
	_setGifCommand = false;
	refreshInlineRows(nullptr, nullptr, true);
	deleteUnusedInlineLayouts();
}

void StickerPanInner::clearInlineRows(bool resultsDeleted) {
	if (resultsDeleted) {
		if (_showingInlineItems) {
			_selected = _pressedSel = -1;
		}
	} else {
		if (_showingInlineItems) {
			clearSelection(true);
		}
		for (InlineRows::const_iterator i = _inlineRows.cbegin(), e = _inlineRows.cend(); i != e; ++i) {
			for (InlineItems::const_iterator j = i->items.cbegin(), end = i->items.cend(); j != end; ++j) {
				(*j)->setPosition(-1);
			}
		}
	}
	_inlineRows.clear();
}

InlineItem *StickerPanInner::layoutPrepareSavedGif(DocumentData *doc, int32 position) {
	auto i = _gifLayouts.constFind(doc);
	if (i == _gifLayouts.cend()) {
		if (auto layout = InlineItem::createLayoutGif(doc)) {
			i = _gifLayouts.insert(doc, layout.release());
			i.value()->initDimensions();
		} else {
			return nullptr;
		}
	}
	if (!i.value()->maxWidth()) return nullptr;

	i.value()->setPosition(position);
	return i.value();
}

InlineItem *StickerPanInner::layoutPrepareInlineResult(InlineResult *result, int32 position) {
	auto i = _inlineLayouts.constFind(result);
	if (i == _inlineLayouts.cend()) {
		if (auto layout = InlineItem::createLayout(result, _inlineWithThumb)) {
			i = _inlineLayouts.insert(result, layout.release());
			i.value()->initDimensions();
		} else {
			return nullptr;
		}
	}
	if (!i.value()->maxWidth()) return nullptr;

	i.value()->setPosition(position);
	return i.value();
}

void StickerPanInner::deleteUnusedGifLayouts() {
	if (_inlineRows.isEmpty() || !_showingSavedGifs) { // delete all
		for_const (const InlineItem *item, _gifLayouts) {
			delete item;
		}
		_gifLayouts.clear();
	} else {
		for (GifLayouts::iterator i = _gifLayouts.begin(); i != _gifLayouts.cend();) {
			if (i.value()->position() < 0) {
				delete i.value();
				i = _gifLayouts.erase(i);
			} else {
				++i;
			}
		}
	}
}

void StickerPanInner::deleteUnusedInlineLayouts() {
	if (_inlineRows.isEmpty() || _showingSavedGifs) { // delete all
		for_const (const InlineItem *item, _inlineLayouts) {
			delete item;
		}
		_inlineLayouts.clear();
	} else {
		for (InlineLayouts::iterator i = _inlineLayouts.begin(); i != _inlineLayouts.cend();) {
			if (i.value()->position() < 0) {
				delete i.value();
				i = _inlineLayouts.erase(i);
			} else {
				++i;
			}
		}
	}
}

StickerPanInner::InlineRow &StickerPanInner::layoutInlineRow(InlineRow &row, int32 sumWidth) {
	int32 count = row.items.size();
	t_assert(count <= InlineItemsMaxPerRow);

	// enumerate items in the order of growing maxWidth()
	// for that sort item indices by maxWidth()
	int indices[InlineItemsMaxPerRow];
	for (int i = 0; i < count; ++i) {
		indices[i] = i;
	}
	std::sort(indices, indices + count, [&row](int a, int b) -> bool {
		return row.items.at(a)->maxWidth() < row.items.at(b)->maxWidth();
	});

	row.height = 0;
	int availw = width() - st::inlineResultsLeft;
	for (int i = 0; i < count; ++i) {
		int index = indices[i];
		int w = sumWidth ? (row.items.at(index)->maxWidth() * availw / sumWidth) : row.items.at(index)->maxWidth();
		int actualw = qMax(w, int(st::inlineResultsMinWidth));
		row.height = qMax(row.height, row.items.at(index)->resizeGetHeight(actualw));
		if (sumWidth) {
			availw -= actualw;
			sumWidth -= row.items.at(index)->maxWidth();
			if (index > 0 && row.items.at(index - 1)->hasRightSkip()) {
				availw -= st::inlineResultsSkip;
				sumWidth -= st::inlineResultsSkip;
			}
		}
	}
	return row;
}

void StickerPanInner::preloadImages() {
	if (_showingInlineItems) {
		for (int32 row = 0, rows = _inlineRows.size(); row < rows; ++row) {
			for (int32 col = 0, cols = _inlineRows.at(row).items.size(); col < cols; ++col) {
				_inlineRows.at(row).items.at(col)->preload();
			}
		}
		return;
	}

	for (int32 i = 0, l = _sets.size(), k = 0; i < l; ++i) {
		for (int32 j = 0, n = _sets.at(i).pack.size(); j < n; ++j) {
			if (++k > StickerPanPerRow * (StickerPanPerRow + 1)) break;

			DocumentData *sticker = _sets.at(i).pack.at(j);
			if (!sticker || !sticker->sticker()) continue;

			bool goodThumb = !sticker->thumb->isNull() && ((sticker->thumb->width() >= 128) || (sticker->thumb->height() >= 128));
			if (goodThumb) {
				sticker->thumb->load();
			} else {
				sticker->automaticLoad(0);
			}
		}
		if (k > StickerPanPerRow * (StickerPanPerRow + 1)) break;
	}
}

uint64 StickerPanInner::currentSet(int yOffset) const {
	if (_showingInlineItems) return Stickers::NoneSetId;

	int y, ytill = 0;
	for (int i = 0, l = _sets.size(); i < l; ++i) {
		int cnt = _sets.at(i).pack.size();
		y = ytill;
		ytill = y + st::emojiPanHeader + ((cnt / StickerPanPerRow) + ((cnt % StickerPanPerRow) ? 1 : 0)) * st::stickerPanSize.height();
		if (yOffset < ytill) {
			return _sets.at(i).id;
		}
	}
	return _sets.isEmpty() ? Stickers::RecentSetId : _sets.back().id;
}

void StickerPanInner::hideInlineRowsPanel() {
	clearInlineRows(false);
	if (_showingInlineItems) {
		_showingSavedGifs = cShowingSavedGifs();
		if (_showingSavedGifs) {
			refreshSavedGifs();
			emit scrollToY(0);
			emit scrollUpdated();
		} else {
			showStickerSet(Stickers::RecentSetId);
		}
	}
}

void StickerPanInner::clearInlineRowsPanel() {
	clearInlineRows(false);
}

void StickerPanInner::refreshSwitchPmButton(const InlineCacheEntry *entry) {
	if (!entry || entry->switchPmText.isEmpty()) {
		_switchPmButton.reset();
		_switchPmStartToken.clear();
	} else {
		if (!_switchPmButton) {
			_switchPmButton = std_::make_unique<BoxButton>(this, QString(), st::switchPmButton);
			_switchPmButton->show();
			_switchPmButton->move(st::inlineResultsLeft, st::emojiPanHeader);
			connect(_switchPmButton.get(), SIGNAL(clicked()), this, SLOT(onSwitchPm()));
		}
		_switchPmButton->setText(entry->switchPmText); // doesn't perform text.toUpper()
		_switchPmStartToken = entry->switchPmStartToken;
	}
	update();
}

int StickerPanInner::refreshInlineRows(UserData *bot, const InlineCacheEntry *entry, bool resultsDeleted) {
	_inlineBot = bot;
	refreshSwitchPmButton(entry);
	auto clearResults = [this, entry]() {
		if (!entry) {
			return true;
		}
		if (entry->results.isEmpty() && entry->switchPmText.isEmpty()) {
			if (!_inlineBot || _inlineBot->username != cInlineGifBotUsername()) {
				return true;
			}
		}
		return false;
	};
	if (clearResults()) {
		if (resultsDeleted) {
			clearInlineRows(true);
		}
		emit emptyInlineRows();
		return 0;
	}

	clearSelection(true);

	t_assert(_inlineBot != 0);
	_inlineBotTitle = lng_inline_bot_results(lt_inline_bot, _inlineBot->username.isEmpty() ? _inlineBot->name : ('@' + _inlineBot->username));

	_showingInlineItems = true;
	_showingSavedGifs = false;
	_settings.hide();

	int32 count = entry->results.size(), from = validateExistingInlineRows(entry->results), added = 0;

	if (count) {
		_inlineRows.reserve(count);
		InlineRow row;
		row.items.reserve(InlineItemsMaxPerRow);
		int32 sumWidth = 0;
		for (int32 i = from; i < count; ++i) {
			if (inlineRowsAddItem(0, entry->results.at(i), row, sumWidth)) {
				++added;
			}
		}
		inlineRowFinalize(row, sumWidth, true);
	}

	int32 h = countHeight();
	if (h != height()) resize(width(), h);
	update();

	emit refreshIcons();

	_lastMousePos = QCursor::pos();
	updateSelected();

	return added;
}

int32 StickerPanInner::validateExistingInlineRows(const InlineResults &results) {
	int32 count = results.size(), until = 0, untilrow = 0, untilcol = 0;
	for (; until < count;) {
		if (untilrow >= _inlineRows.size() || _inlineRows.at(untilrow).items.at(untilcol)->getResult() != results.at(until)) {
			break;
		}
		++until;
		if (++untilcol == _inlineRows.at(untilrow).items.size()) {
			++untilrow;
			untilcol = 0;
		}
	}
	if (until == count) { // all items are layed out
		if (untilrow == _inlineRows.size()) { // nothing changed
			return until;
		}

		for (int32 i = untilrow, l = _inlineRows.size(), skip = untilcol; i < l; ++i) {
			for (int32 j = 0, s = _inlineRows.at(i).items.size(); j < s; ++j) {
				if (skip) {
					--skip;
				} else {
					_inlineRows.at(i).items.at(j)->setPosition(-1);
				}
			}
		}
		if (!untilcol) { // all good rows are filled
			_inlineRows.resize(untilrow);
			return until;
		}
		_inlineRows.resize(untilrow + 1);
		_inlineRows[untilrow].items.resize(untilcol);
		_inlineRows[untilrow] = layoutInlineRow(_inlineRows[untilrow]);
		return until;
	}
	if (untilrow && !untilcol) { // remove last row, maybe it is not full
		--untilrow;
		untilcol = _inlineRows.at(untilrow).items.size();
	}
	until -= untilcol;

	for (int32 i = untilrow, l = _inlineRows.size(); i < l; ++i) {
		for (int32 j = 0, s = _inlineRows.at(i).items.size(); j < s; ++j) {
			_inlineRows.at(i).items.at(j)->setPosition(-1);
		}
	}
	_inlineRows.resize(untilrow);

	if (_inlineRows.isEmpty()) {
		_inlineWithThumb = false;
		for (int32 i = until; i < count; ++i) {
			if (results.at(i)->hasThumbDisplay()) {
				_inlineWithThumb = true;
				break;
			}
		}
	}
	return until;
}

void StickerPanInner::notify_inlineItemLayoutChanged(const InlineItem *layout) {
	if (_selected < 0 || !_showingInlineItems) {
		return;
	}

	int32 row = _selected / MatrixRowShift, col = _selected % MatrixRowShift;
	if (row < _inlineRows.size() && col < _inlineRows.at(row).items.size()) {
		if (layout == _inlineRows.at(row).items.at(col)) {
			updateSelected();
		}
	}
}

void StickerPanInner::ui_repaintInlineItem(const InlineItem *layout) {
	uint64 ms = getms();
	if (_lastScrolled + 100 <= ms) {
		update();
	} else {
		_updateInlineItems.start(_lastScrolled + 100 - ms);
	}
}

bool StickerPanInner::ui_isInlineItemVisible(const InlineItem *layout) {
	int32 position = layout->position();
	if (!_showingInlineItems || position < 0) return false;

	int32 row = position / MatrixRowShift, col = position % MatrixRowShift;
	t_assert((row < _inlineRows.size()) && (col < _inlineRows.at(row).items.size()));

	const InlineItems &inlineItems(_inlineRows.at(row).items);
	int32 top = st::emojiPanHeader;
	for (int32 i = 0; i < row; ++i) {
		top += _inlineRows.at(i).height;
	}

	return (top < _top + _maxHeight) && (top + _inlineRows.at(row).items.at(col)->height() > _top);
}

bool StickerPanInner::ui_isInlineItemBeingChosen() {
	return _showingInlineItems;
}

void StickerPanInner::appendSet(uint64 setId) {
	const Stickers::Sets &sets(Global::StickerSets());
	auto it = sets.constFind(setId);
	if (it == sets.cend() || (it->flags & MTPDstickerSet::Flag::f_disabled) || it->stickers.isEmpty()) return;

	StickerPack pack;
	pack.reserve(it->stickers.size());
	for (int32 i = 0, l = it->stickers.size(); i < l; ++i) {
		pack.push_back(it->stickers.at(i));
	}
	_sets.push_back(DisplayedSet(it->id, it->flags, it->title, pack.size() + 1, pack));
}

void StickerPanInner::refreshRecent() {
	if (_showingInlineItems) {
		if (_showingSavedGifs) {
			refreshSavedGifs();
		}
	} else {
		refreshRecentStickers();
	}
}

void StickerPanInner::refreshRecentStickers(bool performResize) {
	_custom.clear();
	clearSelection(true);
	auto customIt = Global::StickerSets().constFind(Stickers::CustomSetId);
	if (cGetRecentStickers().isEmpty() && (customIt == Global::StickerSets().cend() || customIt->stickers.isEmpty())) {
		if (!_sets.isEmpty() && _sets.at(0).id == Stickers::RecentSetId) {
			_sets.pop_front();
		}
	} else {
		StickerPack recent;
		int32 customCnt = (customIt == Global::StickerSets().cend() ? 0 : customIt->stickers.size());
		QMap<DocumentData*, bool> recentOnly;
		recent.reserve(cGetRecentStickers().size() + customCnt);
		_custom.reserve(cGetRecentStickers().size() + customCnt);
		for (int32 i = 0, l = cGetRecentStickers().size(); i < l; ++i) {
			DocumentData *s = cGetRecentStickers().at(i).first;
			recent.push_back(s);
			recentOnly.insert(s, true);
			_custom.push_back(false);
		}
		for (int32 i = 0; i < customCnt; ++i) {
			DocumentData *s = customIt->stickers.at(i);
			if (recentOnly.contains(s)) {
				_custom[recent.indexOf(s)] = true;
			} else {
				recent.push_back(s);
				_custom.push_back(true);
			}
		}
		if (_sets.isEmpty() || _sets.at(0).id != Stickers::RecentSetId) {
			_sets.push_back(DisplayedSet(Stickers::RecentSetId, MTPDstickerSet::Flag::f_official, lang(lng_emoji_category0), recent.size() * 2, recent));
		} else {
			_sets[0].pack = recent;
			_sets[0].hovers.resize(recent.size() * 2);
		}
	}

	if (performResize && !_showingInlineItems) {
		int32 h = countHeight();
		if (h != height()) {
			resize(width(), h);
			emit needRefreshPanels();
		}

		updateSelected();
	}
}

void StickerPanInner::fillIcons(QList<StickerIcon> &icons) {
	icons.clear();
	icons.reserve(_sets.size() + 1);
	if (!cSavedGifs().isEmpty()) icons.push_back(StickerIcon(Stickers::NoneSetId));

	if (_sets.isEmpty()) return;
	int32 i = 0;
	if (_sets.at(0).id == Stickers::RecentSetId) ++i;
	if (i > 0) icons.push_back(StickerIcon(Stickers::RecentSetId));
	for (int32 l = _sets.size(); i < l; ++i) {
		DocumentData *s = _sets.at(i).pack.at(0);
		int32 availw = st::rbEmoji.width - 2 * st::stickerIconPadding, availh = st::rbEmoji.height - 2 * st::stickerIconPadding;
		int32 thumbw = s->thumb->width(), thumbh = s->thumb->height(), pixw = 1, pixh = 1;
		if (availw * thumbh > availh * thumbw) {
			pixh = availh;
			pixw = (pixh * thumbw) / thumbh;
		} else {
			pixw = availw;
			pixh = thumbw ? ((pixw * thumbh) / thumbw) : 1;
		}
		if (pixw < 1) pixw = 1;
		if (pixh < 1) pixh = 1;
		icons.push_back(StickerIcon(_sets.at(i).id, s, pixw, pixh));
	}
}

void StickerPanInner::fillPanels(QVector<EmojiPanel*> &panels) {
	for (int32 i = 0; i < panels.size(); ++i) {
		panels.at(i)->hide();
		panels.at(i)->deleteLater();
	}
	panels.clear();

	if (_showingInlineItems) {
		panels.push_back(new EmojiPanel(parentWidget(), _showingSavedGifs ? lang(lng_saved_gifs) : _inlineBotTitle, Stickers::NoneSetId, true, 0));
		panels.back()->show();
		return;
	}

	if (_sets.isEmpty()) return;

	int y = 0;
	panels.reserve(_sets.size());
	for (int32 i = 0, l = _sets.size(); i < l; ++i) {
		bool special = (_sets.at(i).flags & MTPDstickerSet::Flag::f_official);
		panels.push_back(new EmojiPanel(parentWidget(), _sets.at(i).title, _sets.at(i).id, special, y));
		panels.back()->show();
		connect(panels.back(), SIGNAL(deleteClicked(quint64)), this, SIGNAL(removing(quint64)));
		int cnt = _sets.at(i).pack.size(), rows = (cnt / StickerPanPerRow) + ((cnt % StickerPanPerRow) ? 1 : 0);
		int h = st::emojiPanHeader + rows * st::stickerPanSize.height();
		y += h;
	}
}

void StickerPanInner::refreshPanels(QVector<EmojiPanel*> &panels) {
	if (_showingInlineItems) return;

	if (panels.size() != _sets.size()) return fillPanels(panels);

	int32 y = 0;
	for (int32 i = 0, l = _sets.size(); i < l; ++i) {
		panels.at(i)->setWantedY(y);
		int cnt = _sets.at(i).pack.size(), rows = (cnt / StickerPanPerRow) + ((cnt % StickerPanPerRow) ? 1 : 0);
		int h = st::emojiPanHeader + rows * st::stickerPanSize.height();
		y += h;
	}
}

void StickerPanInner::updateSelected() {
	if (_pressedSel >= 0 && !_previewShown) {
		return;
	}

	int32 selIndex = -1;
	QPoint p(mapFromGlobal(_lastMousePos));

	if (_showingInlineItems) {
		int sx = (rtl() ? width() - p.x() : p.x()) - st::inlineResultsLeft;
		int sy = p.y() - st::emojiPanHeader;
		if (_switchPmButton) {
			sy -= _switchPmButton->height() + st::inlineResultsSkip;
		}
		int row = -1, col = -1, sel = -1;
		ClickHandlerPtr lnk;
		ClickHandlerHost *lnkhost = nullptr;
		HistoryCursorState cursor = HistoryDefaultCursorState;
		if (sy >= 0) {
			row = 0;
			for (int rows = _inlineRows.size(); row < rows; ++row) {
				if (sy < _inlineRows.at(row).height) {
					break;
				}
				sy -= _inlineRows.at(row).height;
			}
		}
		if (sx >= 0 && row >= 0 && row < _inlineRows.size()) {
			const InlineItems &inlineItems(_inlineRows.at(row).items);
			col = 0;
			for (int32 cols = inlineItems.size(); col < cols; ++col) {
				int32 width = inlineItems.at(col)->width();
				if (sx < width) {
					break;
				}
				sx -= width;
				if (inlineItems.at(col)->hasRightSkip()) {
					sx -= st::inlineResultsSkip;
				}
			}
			if (col < inlineItems.size()) {
				sel = row * MatrixRowShift + col;
				inlineItems.at(col)->getState(lnk, cursor, sx, sy);
				lnkhost = inlineItems.at(col);
			} else {
				row = col = -1;
			}
		} else {
			row = col = -1;
		}
		int32 srow = (_selected >= 0) ? (_selected / MatrixRowShift) : -1;
		int32 scol = (_selected >= 0) ? (_selected % MatrixRowShift) : -1;
		if (_selected != sel) {
			if (srow >= 0 && scol >= 0) {
				t_assert(srow >= 0 && srow < _inlineRows.size() && scol >= 0 && scol < _inlineRows.at(srow).items.size());
				Ui::repaintInlineItem(_inlineRows.at(srow).items.at(scol));
			}
			_selected = sel;
			if (row >= 0 && col >= 0) {
				t_assert(row >= 0 && row < _inlineRows.size() && col >= 0 && col < _inlineRows.at(row).items.size());
				Ui::repaintInlineItem(_inlineRows.at(row).items.at(col));
			}
			if (_pressedSel >= 0 && _selected >= 0 && _pressedSel != _selected) {
				_pressedSel = _selected;
				if (row >= 0 && col >= 0) {
					auto layout = _inlineRows.at(row).items.at(col);
					if (auto previewDocument = layout->getPreviewDocument()) {
						Ui::showMediaPreview(previewDocument);
					} else if (auto previewPhoto = layout->getPreviewPhoto()) {
						Ui::showMediaPreview(previewPhoto);
					}
				}
			}
		}
		if (ClickHandler::setActive(lnk, lnkhost)) {
			setCursor(lnk ? style::cur_pointer : style::cur_default);
		}
		return;
	}

	int y, ytill = 0, sx = (rtl() ? width() - p.x() : p.x()) - st::stickerPanPadding;
	for (int c = 0, l = _sets.size(); c < l; ++c) {
		const DisplayedSet &set(_sets.at(c));
		int cnt = set.pack.size();
		bool special = (set.flags & MTPDstickerSet::Flag::f_official);

		y = ytill;
		ytill = y + st::emojiPanHeader + ((cnt / StickerPanPerRow) + ((cnt % StickerPanPerRow) ? 1 : 0)) * st::stickerPanSize.height();
		if (p.y() >= y && p.y() < ytill) {
			y += st::emojiPanHeader;
			if (p.y() >= y && sx >= 0 && sx < StickerPanPerRow * st::stickerPanSize.width()) {
				selIndex = qFloor((p.y() - y) / st::stickerPanSize.height()) * StickerPanPerRow + qFloor(sx / st::stickerPanSize.width());
				if (selIndex >= set.pack.size()) {
					selIndex = -1;
				} else {
					if (set.id == Stickers::RecentSetId && _custom[selIndex]) {
						int32 inx = sx - (selIndex % StickerPanPerRow) * st::stickerPanSize.width(), iny = p.y() - y - ((selIndex / StickerPanPerRow) * st::stickerPanSize.height());
						if (inx >= st::stickerPanSize.width() - st::stickerPanDelete.pxWidth() && iny < st::stickerPanDelete.pxHeight()) {
							selIndex += set.pack.size();
						}
					}
					selIndex += c * MatrixRowShift;
				}
			}
			break;
		}
	}

	bool startanim = false;
	int oldSel = _selected, oldSelTab = oldSel / MatrixRowShift, xOldSel = -1, newSel = selIndex, newSelTab = newSel / MatrixRowShift, xNewSel = -1;
	if (oldSel >= 0 && oldSelTab < _sets.size() && _sets[oldSelTab].id == Stickers::RecentSetId && oldSel >= oldSelTab * MatrixRowShift + _sets[oldSelTab].pack.size()) {
		xOldSel = oldSel;
		oldSel -= _sets[oldSelTab].pack.size();
	}
	if (newSel >= 0 && newSelTab < _sets.size() && _sets[newSelTab].id == Stickers::RecentSetId && newSel >= newSelTab * MatrixRowShift + _sets[newSelTab].pack.size()) {
		xNewSel = newSel;
		newSel -= _sets[newSelTab].pack.size();
	}
	if (newSel != oldSel) {
		if (oldSel >= 0) {
			_animations.remove(oldSel + 1);
			if (_animations.find(-oldSel - 1) == _animations.end()) {
				if (_animations.isEmpty()) startanim = true;
				_animations.insert(-oldSel - 1, getms());
			}
		}
		if (newSel >= 0) {
			_animations.remove(-newSel - 1);
			if (_animations.find(newSel + 1) == _animations.end()) {
				if (_animations.isEmpty()) startanim = true;
				_animations.insert(newSel + 1, getms());
			}
		}
		setCursor((newSel >= 0) ? style::cur_pointer : style::cur_default);
	}
	if (xNewSel != xOldSel) {
		if (xOldSel >= 0) {
			_animations.remove(xOldSel + 1);
			if (_animations.find(-xOldSel - 1) == _animations.end()) {
				if (_animations.isEmpty()) startanim = true;
				_animations.insert(-xOldSel - 1, getms());
			}
		}
		if (xNewSel >= 0) {
			_animations.remove(-xNewSel - 1);
			if (_animations.find(xNewSel + 1) == _animations.end()) {
				if (_animations.isEmpty()) startanim = true;
				_animations.insert(xNewSel + 1, getms());
			}
		}
	}
	_selected = selIndex;
	if (_pressedSel >= 0 && _selected >= 0 && _pressedSel != _selected) {
		_pressedSel = _selected;
		if (newSel >= 0 && xNewSel < 0) {
			Ui::showMediaPreview(_sets.at(newSelTab).pack.at(newSel % MatrixRowShift));
		}
	}
	if (startanim && !_a_selected.animating()) _a_selected.start();
}

void StickerPanInner::onSettings() {
	Ui::showLayer(new StickersBox());
}

void StickerPanInner::onPreview() {
	if (_pressedSel < 0) return;
	if (_showingInlineItems) {
		int32 row = _pressedSel / MatrixRowShift, col = _pressedSel % MatrixRowShift;
		if (row < _inlineRows.size() && col < _inlineRows.at(row).items.size()) {
			auto layout = _inlineRows.at(row).items.at(col);
			if (auto previewDocument = layout->getPreviewDocument()) {
				Ui::showMediaPreview(previewDocument);
				_previewShown = true;
			} else if (auto previewPhoto = layout->getPreviewPhoto()) {
				Ui::showMediaPreview(previewPhoto);
				_previewShown = true;
			}
		}
	} else if (_pressedSel < MatrixRowShift * _sets.size()) {
		int tab = (_pressedSel / MatrixRowShift), sel = _pressedSel % MatrixRowShift;
		if (sel < _sets.at(tab).pack.size()) {
			Ui::showMediaPreview(_sets.at(tab).pack.at(sel));
			_previewShown = true;
		}
	}
}

void StickerPanInner::onUpdateInlineItems() {
	if (!_showingInlineItems) return;

	uint64 ms = getms();
	if (_lastScrolled + 100 <= ms) {
		update();
	} else {
		_updateInlineItems.start(_lastScrolled + 100 - ms);
	}
}

void StickerPanInner::onSwitchPm() {
	if (_inlineBot && _inlineBot->botInfo) {
		_inlineBot->botInfo->startToken = _switchPmStartToken;
		Ui::showPeerHistory(_inlineBot, ShowAndStartBotMsgId);
	}
}

void StickerPanInner::step_selected(uint64 ms, bool timer) {
	QRegion toUpdate;
	for (Animations::iterator i = _animations.begin(); i != _animations.end();) {
		int index = qAbs(i.key()) - 1, tab = (index / MatrixRowShift), sel = index % MatrixRowShift;
		float64 dt = float64(ms - i.value()) / st::emojiPanDuration;
		if (dt >= 1) {
			_sets[tab].hovers[sel] = (i.key() > 0) ? 1 : 0;
			i = _animations.erase(i);
		} else {
			_sets[tab].hovers[sel] = (i.key() > 0) ? dt : (1 - dt);
			++i;
		}
		toUpdate += stickerRect(tab, sel);
	}
	if (timer) rtlupdate(toUpdate.boundingRect());
	if (_animations.isEmpty()) _a_selected.stop();
}

void StickerPanInner::showStickerSet(uint64 setId) {
	clearSelection(true);

	if (setId == Stickers::NoneSetId) {
		bool wasNotShowingGifs = !_showingInlineItems;
		if (wasNotShowingGifs) {
			_showingInlineItems = true;
			_showingSavedGifs = true;
			cSetShowingSavedGifs(true);
			emit saveConfigDelayed(SaveRecentEmojisTimeout);
		}
		refreshSavedGifs();
		emit scrollToY(0);
		emit scrollUpdated();
		showFinish();
		return;
	}

	if (_showingInlineItems) {
		if (_setGifCommand && _showingSavedGifs) {
			App::insertBotCommand(qsl(""), true);
		}
		_setGifCommand = false;

		_showingInlineItems = false;
		cSetShowingSavedGifs(false);
		emit saveConfigDelayed(SaveRecentEmojisTimeout);

		Notify::clipStopperHidden(ClipStopperSavedGifsPanel);

		refreshRecentStickers(true);
		emit refreshIcons();
	}

	int32 y = 0;
	for (int c = 0; c < _sets.size(); ++c) {
		if (_sets.at(c).id == setId) break;
		int rows = (_sets[c].pack.size() / StickerPanPerRow) + ((_sets[c].pack.size() % StickerPanPerRow) ? 1 : 0);
		y += st::emojiPanHeader + rows * st::stickerPanSize.height();
	}

	emit scrollToY(y);
	emit scrollUpdated();

	_lastMousePos = QCursor::pos();

	update();
}

void StickerPanInner::updateShowingSavedGifs() {
	if (cShowingSavedGifs()) {
		if (!_showingInlineItems) {
			clearSelection(true);
			_showingSavedGifs = _showingInlineItems = true;
			if (_inlineRows.isEmpty()) refreshSavedGifs();
		}
	} else if (!_showingInlineItems) {
		clearSelection(true);
		_showingSavedGifs = false;
	}
}

void StickerPanInner::showFinish() {
	if (_showingInlineItems && _showingSavedGifs) {
		_setGifCommand = App::insertBotCommand('@' + cInlineGifBotUsername(), true);
	}
}

EmojiPanel::EmojiPanel(QWidget *parent, const QString &text, uint64 setId, bool special, int32 wantedY) : TWidget(parent)
, _wantedY(wantedY)
, _setId(setId)
, _special(special)
, _deleteVisible(false)
, _delete(special ? 0 : new IconedButton(this, st::notifyClose)) { // Stickers::NoneSetId if in emoji
	resize(st::emojiPanWidth, st::emojiPanHeader);
	setMouseTracking(true);
	setFocusPolicy(Qt::NoFocus);
	setText(text);
	if (_delete) {
		_delete->hide();
		_delete->moveToRight(st::emojiPanHeaderLeft - ((_delete->width() - st::notifyClose.icon.pxWidth()) / 2), (st::emojiPanHeader - _delete->height()) / 2, width());
		connect(_delete, SIGNAL(clicked()), this, SLOT(onDelete()));
	}
}

void EmojiPanel::onDelete() {
	emit deleteClicked(_setId);
}

void EmojiPanel::setText(const QString &text) {
	_fullText = text;
	updateText();
}

void EmojiPanel::updateText() {
	int32 availw = st::emojiPanWidth - st::emojiPanHeaderLeft * 2;
	if (_deleteVisible) {
		if (!_special && _setId != Stickers::NoneSetId) {
			availw -= st::notifyClose.icon.pxWidth() + st::emojiPanHeaderLeft;
		}
	} else {
		auto switchText = ([this]() {
			if (_setId != Stickers::NoneSetId) {
				return lang(lng_switch_emoji);
			}
			if (cSavedGifs().isEmpty()) {
				return lang(lng_switch_stickers);
			}
			return lang(lng_switch_stickers_gifs);
		})();
		availw -= st::emojiSwitchSkip + st::emojiPanHeaderFont->width(switchText);
	}
	_text = st::emojiPanHeaderFont->elided(_fullText, availw);
	update();
}

void EmojiPanel::setDeleteVisible(bool isVisible) {
	if (_deleteVisible != isVisible) {
		_deleteVisible = isVisible;
		updateText();
		if (_delete) {
			_delete->setVisible(_deleteVisible);
		}
	}
}

void EmojiPanel::mousePressEvent(QMouseEvent *e) {
	emit mousePressed();
}

void EmojiPanel::paintEvent(QPaintEvent *e) {
	Painter p(this);

	if (!_deleteVisible) {
		p.fillRect(0, 0, width(), st::emojiPanHeader, st::emojiPanHeaderBg->b);
	}
	p.setFont(st::emojiPanHeaderFont->f);
	p.setPen(st::emojiPanHeaderColor->p);
	p.drawTextLeft(st::emojiPanHeaderLeft, st::emojiPanHeaderTop, width(), _text);
}

EmojiSwitchButton::EmojiSwitchButton(QWidget *parent, bool toStickers) : Button(parent)
, _toStickers(toStickers) {
	setCursor(style::cur_pointer);
	updateText();
}

void EmojiSwitchButton::updateText(const QString &inlineBotUsername) {
	if (_toStickers) {
		if (inlineBotUsername.isEmpty()) {
			_text = lang(cSavedGifs().isEmpty() ? lng_switch_stickers : lng_switch_stickers_gifs);
		} else {
			_text = '@' + inlineBotUsername;
		}
	} else {
		_text = lang(lng_switch_emoji);
	}
	_textWidth = st::emojiPanHeaderFont->width(_text);
	if (_toStickers && !inlineBotUsername.isEmpty()) {
		int32 maxw = 0;
		for (int c = 0; c < emojiTabCount; ++c) {
			maxw = qMax(maxw, st::emojiPanHeaderFont->width(lang(LangKey(lng_emoji_category0 + c))));
		}
		maxw += st::emojiPanHeaderLeft + st::emojiSwitchSkip + (st::emojiSwitchSkip - st::emojiSwitchImgSkip);
		if (_textWidth > st::emojiPanWidth - maxw) {
			_text = st::emojiPanHeaderFont->elided(_text, st::emojiPanWidth - maxw);
			_textWidth = st::emojiPanHeaderFont->width(_text);
		}
	}

	int32 w = st::emojiSwitchSkip + _textWidth + (st::emojiSwitchSkip - st::emojiSwitchImgSkip);
	resize(w, st::emojiPanHeader);
}

void EmojiSwitchButton::paintEvent(QPaintEvent *e) {
	Painter p(this);

	p.setFont(st::emojiPanHeaderFont->f);
	p.setPen(st::emojiSwitchColor->p);
	if (_toStickers) {
		p.drawTextRight(st::emojiSwitchSkip, st::emojiPanHeaderTop, width(), _text, _textWidth);
		p.drawSpriteRight(QPoint(st::emojiSwitchImgSkip - st::emojiSwitchStickers.pxWidth(), (st::emojiPanHeader - st::emojiSwitchStickers.pxHeight()) / 2), width(), st::emojiSwitchStickers);
	} else {
		p.drawTextRight(st::emojiSwitchImgSkip - st::emojiSwitchEmoji.pxWidth(), st::emojiPanHeaderTop, width(), lang(lng_switch_emoji), _textWidth);
		p.drawSpriteRight(QPoint(st::emojiSwitchSkip + _textWidth - st::emojiSwitchEmoji.pxWidth(), (st::emojiPanHeader - st::emojiSwitchEmoji.pxHeight()) / 2), width(), st::emojiSwitchEmoji);
	}
}

} // namespace internal

EmojiPan::EmojiPan(QWidget *parent) : TWidget(parent)
, _maxHeight(st::emojiPanMaxHeight)
, _contentMaxHeight(st::emojiPanMaxHeight)
, _contentHeight(_contentMaxHeight)
, _contentHeightEmoji(_contentHeight - st::rbEmoji.height)
, _contentHeightStickers(_contentHeight - st::rbEmoji.height)
, _horizontal(false)
, _noTabUpdate(false)
, _hiding(false)
, a_opacity(0)
, _a_appearance(animation(this, &EmojiPan::step_appearance))
, _shadow(st::dropdownDef.shadow)
, _recent(this  , qsl("emoji_group"), dbietRecent  , QString(), true , st::rbEmojiRecent)
, _people(this  , qsl("emoji_group"), dbietPeople  , QString(), false, st::rbEmojiPeople)
, _nature(this  , qsl("emoji_group"), dbietNature  , QString(), false, st::rbEmojiNature)
, _food(this    , qsl("emoji_group"), dbietFood    , QString(), false, st::rbEmojiFood)
, _activity(this, qsl("emoji_group"), dbietActivity, QString(), false, st::rbEmojiActivity)
, _travel(this  , qsl("emoji_group"), dbietTravel  , QString(), false, st::rbEmojiTravel)
, _objects(this , qsl("emoji_group"), dbietObjects , QString(), false, st::rbEmojiObjects)
, _symbols(this , qsl("emoji_group"), dbietSymbols , QString(), false, st::rbEmojiSymbols)
, _iconOver(-1)
, _iconSel(0)
, _iconDown(-1)
, _iconsDragging(false)
, _a_icons(animation(this, &EmojiPan::step_icons))
, _iconsLeft(0)
, _iconsTop(0)
, _iconsStartX(0)
, _iconsMax(0)
, _iconsX(0, 0)
, _iconSelX(0, 0)
, _iconsStartAnim(0)
, _stickersShown(false)
, _shownFromInlineQuery(false)
, _a_slide(animation(this, &EmojiPan::step_slide))
, e_scroll(this, st::emojiScroll)
, e_inner()
, e_switch(&e_scroll, true)
, s_scroll(this, st::emojiScroll)
, s_inner()
, s_switch(&s_scroll, false)
, _removingSetId(0)
, _inlineBot(0)
, _inlineRequestId(0) {
	setFocusPolicy(Qt::NoFocus);
	e_scroll.setFocusPolicy(Qt::NoFocus);
	e_scroll.viewport()->setFocusPolicy(Qt::NoFocus);
	s_scroll.setFocusPolicy(Qt::NoFocus);
	s_scroll.viewport()->setFocusPolicy(Qt::NoFocus);

	_width = st::dropdownDef.padding.left() + st::emojiPanWidth + st::dropdownDef.padding.right();
	_height = st::dropdownDef.padding.top() + _contentHeight + st::dropdownDef.padding.bottom();
	_bottom = 0;
	resize(_width, _height);

	e_scroll.resize(st::emojiPanWidth, _contentHeightEmoji);
	s_scroll.resize(st::emojiPanWidth, _contentHeightStickers);

	e_scroll.move(st::dropdownDef.padding.left(), st::dropdownDef.padding.top());
	e_scroll.setWidget(&e_inner);
	s_scroll.move(st::dropdownDef.padding.left(), st::dropdownDef.padding.top());
	s_scroll.setWidget(&s_inner);

	e_inner.moveToLeft(0, 0, e_scroll.width());
	s_inner.moveToLeft(0, 0, s_scroll.width());

	int32 left = _iconsLeft = st::dropdownDef.padding.left() + (st::emojiPanWidth - 8 * st::rbEmoji.width) / 2;
	int32 top = _iconsTop = st::dropdownDef.padding.top() + _contentHeight - st::rbEmoji.height;
	prepareTab(left, top, _width, _recent);
	prepareTab(left, top, _width, _people);
	prepareTab(left, top, _width, _nature);
	prepareTab(left, top, _width, _food);
	prepareTab(left, top, _width, _activity);
	prepareTab(left, top, _width, _travel);
	prepareTab(left, top, _width, _objects);
	prepareTab(left, top, _width, _symbols);
	e_inner.fillPanels(e_panels);
	updatePanelsPositions(e_panels, 0);

	_hideTimer.setSingleShot(true);
	connect(&_hideTimer, SIGNAL(timeout()), this, SLOT(hideStart()));

	connect(&e_inner, SIGNAL(scrollToY(int)), &e_scroll, SLOT(scrollToY(int)));
	connect(&e_inner, SIGNAL(disableScroll(bool)), &e_scroll, SLOT(disableScroll(bool)));

	connect(&s_inner, SIGNAL(scrollToY(int)), &s_scroll, SLOT(scrollToY(int)));
	connect(&s_inner, SIGNAL(scrollUpdated()), this, SLOT(onScrollStickers()));

	connect(&e_scroll, SIGNAL(scrolled()), this, SLOT(onScrollEmoji()));
	connect(&s_scroll, SIGNAL(scrolled()), this, SLOT(onScrollStickers()));

	connect(&e_inner, SIGNAL(selected(EmojiPtr)), this, SIGNAL(emojiSelected(EmojiPtr)));
	connect(&s_inner, SIGNAL(selected(DocumentData*)), this, SIGNAL(stickerSelected(DocumentData*)));
	connect(&s_inner, SIGNAL(selected(PhotoData*)), this, SIGNAL(photoSelected(PhotoData*)));
	connect(&s_inner, SIGNAL(selected(InlineBots::Result*,UserData*)), this, SIGNAL(inlineResultSelected(InlineBots::Result*,UserData*)));

	connect(&s_inner, SIGNAL(emptyInlineRows()), this, SLOT(onEmptyInlineRows()));

	connect(&s_switch, SIGNAL(clicked()), this, SLOT(onSwitch()));
	connect(&e_switch, SIGNAL(clicked()), this, SLOT(onSwitch()));
	s_switch.moveToRight(0, 0, st::emojiPanWidth);
	e_switch.moveToRight(0, 0, st::emojiPanWidth);

	connect(&s_inner, SIGNAL(removing(quint64)), this, SLOT(onRemoveSet(quint64)));
	connect(&s_inner, SIGNAL(refreshIcons()), this, SLOT(onRefreshIcons()));
	connect(&e_inner, SIGNAL(needRefreshPanels()), this, SLOT(onRefreshPanels()));
	connect(&s_inner, SIGNAL(needRefreshPanels()), this, SLOT(onRefreshPanels()));

	_saveConfigTimer.setSingleShot(true);
	connect(&_saveConfigTimer, SIGNAL(timeout()), this, SLOT(onSaveConfig()));
	connect(&e_inner, SIGNAL(saveConfigDelayed(int32)), this, SLOT(onSaveConfigDelayed(int32)));
	connect(&s_inner, SIGNAL(saveConfigDelayed(int32)), this, SLOT(onSaveConfigDelayed(int32)));

	// inline bots
	_inlineRequestTimer.setSingleShot(true);
	connect(&_inlineRequestTimer, SIGNAL(timeout()), this, SLOT(onInlineRequest()));

	if (cPlatform() == dbipMac || cPlatform() == dbipMacOld) {
		connect(App::wnd()->windowHandle(), SIGNAL(activeChanged()), this, SLOT(onWndActiveChanged()));
	}

	setMouseTracking(true);
//	setAttribute(Qt::WA_AcceptTouchEvents);
}

void EmojiPan::setMaxHeight(int32 h) {
	_maxHeight = h;
	updateContentHeight();
}

void EmojiPan::updateContentHeight() {
	int32 h = qMin(_contentMaxHeight, _maxHeight);
	int32 he = h - st::rbEmoji.height;
	int32 hs = h - (s_inner.showSectionIcons() ? st::rbEmoji.height : 0);
	if (h == _contentHeight && he == _contentHeightEmoji && hs == _contentHeightStickers) return;

	int32 was = _contentHeight, wase = _contentHeightEmoji, wass = _contentHeightStickers;
	_contentHeight = h;
	_contentHeightEmoji = he;
	_contentHeightStickers = hs;

	_height = st::dropdownDef.padding.top() + _contentHeight + st::dropdownDef.padding.bottom();

	resize(_width, _height);
	move(x(), _bottom - height());

	if (was > _contentHeight || (was == _contentHeight && wass > _contentHeightStickers)) {
		e_scroll.resize(st::emojiPanWidth, _contentHeightEmoji);
		s_scroll.resize(st::emojiPanWidth, _contentHeightStickers);
		s_inner.setMaxHeight(_contentHeightStickers);
		e_inner.setMaxHeight(_contentHeightEmoji);
	} else {
		s_inner.setMaxHeight(_contentHeightStickers);
		e_inner.setMaxHeight(_contentHeightEmoji);
		e_scroll.resize(st::emojiPanWidth, _contentHeightEmoji);
		s_scroll.resize(st::emojiPanWidth, _contentHeightStickers);
	}

	_iconsTop = st::dropdownDef.padding.top() + _contentHeight - st::rbEmoji.height;
	_recent.move(_recent.x(), _iconsTop);
	_people.move(_people.x(), _iconsTop);
	_nature.move(_nature.x(), _iconsTop);
	_food.move(_food.x(), _iconsTop);
	_activity.move(_activity.x(), _iconsTop);
	_travel.move(_travel.x(), _iconsTop);
	_objects.move(_objects.x(), _iconsTop);
	_symbols.move(_symbols.x(), _iconsTop);

	update();
}

void EmojiPan::prepareTab(int32 &left, int32 top, int32 _width, FlatRadiobutton &tab) {
	tab.moveToLeft(left, top, _width);
	left += tab.width();
	tab.setAttribute(Qt::WA_OpaquePaintEvent);
	connect(&tab, SIGNAL(changed()), this, SLOT(onTabChange()));
}

void EmojiPan::onWndActiveChanged() {
	if (!App::wnd()->windowHandle()->isActive() && !isHidden()) {
		leaveEvent(0);
	}
}

void EmojiPan::onSaveConfig() {
	Local::writeUserSettings();
}

void EmojiPan::onSaveConfigDelayed(int32 delay) {
	_saveConfigTimer.start(delay);
}

void EmojiPan::paintEvent(QPaintEvent *e) {
	Painter p(this);

	float64 o = 1;
	if (!_cache.isNull()) {
		p.setOpacity(o = a_opacity.current());
	}

	QRect r(st::dropdownDef.padding.left(), st::dropdownDef.padding.top(), _width - st::dropdownDef.padding.left() - st::dropdownDef.padding.right(), _height - st::dropdownDef.padding.top() - st::dropdownDef.padding.bottom());

	_shadow.paint(p, r, st::dropdownDef.shadowShift);

	if (_toCache.isNull()) {
		if (_cache.isNull()) {
			p.fillRect(myrtlrect(r.x() + r.width() - st::emojiScroll.width, r.y(), st::emojiScroll.width, e_scroll.height()), st::white->b);
			if (_stickersShown && s_inner.showSectionIcons()) {
				p.fillRect(r.left(), _iconsTop, r.width(), st::rbEmoji.height, st::emojiPanCategories);
				p.drawSpriteLeft(_iconsLeft + 7 * st::rbEmoji.width + st::rbEmojiRecent.imagePos.x(), _iconsTop + st::rbEmojiRecent.imagePos.y(), width(), st::stickersSettings);

				if (!_icons.isEmpty()) {
					int32 x = _iconsLeft, i = 0, selxrel = _iconsLeft + _iconSelX.current(), selx = selxrel - _iconsX.current();
					for (int32 l = _icons.size(); i < l && !_icons.at(i).sticker; ++i) {
						bool gifs = (_icons.at(i).setId == Stickers::NoneSetId);
						if (selxrel != x) {
							p.drawSpriteLeft(x + st::rbEmojiRecent.imagePos.x(), _iconsTop + st::rbEmojiRecent.imagePos.y(), width(), gifs ? st::savedGifsOver : st::rbEmojiRecent.imageRect);
						}
						if (selxrel < x + st::rbEmoji.width && selxrel > x - st::rbEmoji.width) {
							p.setOpacity(1 - (qAbs(selxrel - x) / st::rbEmoji.width));
							p.drawSpriteLeft(x + st::rbEmojiRecent.imagePos.x(), _iconsTop + st::rbEmojiRecent.imagePos.y(), width(), gifs ? st::savedGifsActive : st::rbEmojiRecent.chkImageRect);
							p.setOpacity(1);
						}
						x += st::rbEmoji.width;
					}
					int32 skip = i;

					QRect clip(x, _iconsTop, _iconsLeft + 7 * st::rbEmoji.width - x, st::rbEmoji.height);
					if (rtl()) clip.moveLeft(width() - x - clip.width());
					p.setClipRect(clip);

					i += _iconsX.current() / int(st::rbEmoji.width);
					x -= _iconsX.current() % int(st::rbEmoji.width);
					for (int32 l = qMin(_icons.size(), i + 8 - skip); i < l; ++i) {
						const internal::StickerIcon &s(_icons.at(i));
						s.sticker->thumb->load();
						QPixmap pix(s.sticker->thumb->pix(s.pixw, s.pixh));

						p.drawPixmapLeft(x + (st::rbEmoji.width - s.pixw) / 2, _iconsTop + (st::rbEmoji.height - s.pixh) / 2, width(), pix);
						x += st::rbEmoji.width;
					}

					if (rtl()) selx = width() - selx - st::rbEmoji.width;
					p.setOpacity(skip ? qMax(1., selx / float64(skip * st::rbEmoji.width)) : 1.);
					p.fillRect(selx, _iconsTop + st::rbEmoji.height - st::stickerIconPadding, st::rbEmoji.width, st::stickerIconSel, st::stickerIconSelColor);

					float64 o_left = snap(float64(_iconsX.current()) / st::stickerIconLeft.pxWidth(), 0., 1.);
					if (o_left > 0) {
						p.setOpacity(o_left);
						p.drawSpriteLeft(QRect(_iconsLeft + skip * st::rbEmoji.width, _iconsTop, st::stickerIconLeft.pxWidth(), st::rbEmoji.height), width(), st::stickerIconLeft);
					}
					float64 o_right = snap(float64(_iconsMax - _iconsX.current()) / st::stickerIconRight.pxWidth(), 0., 1.);
					if (o_right > 0) {
						p.setOpacity(o_right);
						p.drawSpriteRight(QRect(width() - _iconsLeft - 7 * st::rbEmoji.width, _iconsTop, st::stickerIconRight.pxWidth(), st::rbEmoji.height), width(), st::stickerIconRight);
					}
				}
			} else if (_stickersShown) {
				int32 x = rtl() ? (_recent.x() + _recent.width()) : (_objects.x() + _objects.width());
				p.fillRect(x, _recent.y(), r.left() + r.width() - x, st::rbEmoji.height, st::white);
			} else {
				p.fillRect(r.left(), _recent.y(), (rtl() ? _objects.x() : _recent.x() - r.left()), st::rbEmoji.height, st::emojiPanCategories);
				int32 x = rtl() ? (_recent.x() + _recent.width()) : (_objects.x() + _objects.width());
				p.fillRect(x, _recent.y(), r.left() + r.width() - x, st::rbEmoji.height, st::emojiPanCategories);
			}
		} else {
			p.fillRect(r, st::white->b);
			p.drawPixmap(r.left(), r.top(), _cache);
		}
	} else {
		p.fillRect(QRect(r.left(), r.top(), r.width(), r.height() - st::rbEmoji.height), st::white->b);
		p.fillRect(QRect(r.left(), _iconsTop, r.width(), st::rbEmoji.height), st::emojiPanCategories->b);
		p.setOpacity(o * a_fromAlpha.current());
		QRect fromDst = QRect(r.left() + a_fromCoord.current(), r.top(), _fromCache.width() / cIntRetinaFactor(), _fromCache.height() / cIntRetinaFactor());
		QRect fromSrc = QRect(0, 0, _fromCache.width(), _fromCache.height());
		if (fromDst.x() < r.left() + r.width() && fromDst.x() + fromDst.width() > r.left()) {
			if (fromDst.x() < r.left()) {
				fromSrc.setX((r.left() - fromDst.x()) * cIntRetinaFactor());
				fromDst.setX(r.left());
			} else if (fromDst.x() + fromDst.width() > r.left() + r.width()) {
				fromSrc.setWidth((r.left() + r.width() - fromDst.x()) * cIntRetinaFactor());
				fromDst.setWidth(r.left() + r.width() - fromDst.x());
			}
			p.drawPixmap(fromDst, _fromCache, fromSrc);
		}
		p.setOpacity(o * a_toAlpha.current());
		QRect toDst = QRect(r.left() + a_toCoord.current(), r.top(), _toCache.width() / cIntRetinaFactor(), _toCache.height() / cIntRetinaFactor());
		QRect toSrc = QRect(0, 0, _toCache.width(), _toCache.height());
		if (toDst.x() < r.left() + r.width() && toDst.x() + toDst.width() > r.left()) {
			if (toDst.x() < r.left()) {
				toSrc.setX((r.left() - toDst.x()) * cIntRetinaFactor());
				toDst.setX(r.left());
			} else if (toDst.x() + toDst.width() > r.left() + r.width()) {
				toSrc.setWidth((r.left() + r.width() - toDst.x()) * cIntRetinaFactor());
				toDst.setWidth(r.left() + r.width() - toDst.x());
			}
			p.drawPixmap(toDst, _toCache, toSrc);
		}
	}
}

void EmojiPan::moveBottom(int32 bottom, bool force) {
	_bottom = bottom;
	if (isHidden() && !force) {
		move(x(), _bottom - height());
		return;
	}
	if (_stickersShown && s_inner.inlineResultsShown()) {
		moveToLeft(0, _bottom - height());
	} else {
		moveToRight(0, _bottom - height());
	}
}

void EmojiPan::enterEvent(QEvent *e) {
	_hideTimer.stop();
	if (_hiding) showStart();
}

void EmojiPan::leaveEvent(QEvent *e) {
	if (_removingSetId || s_inner.inlineResultsShown()) return;
	if (_a_appearance.animating()) {
		hideStart();
	} else {
		_hideTimer.start(300);
	}
}

void EmojiPan::otherEnter() {
	_hideTimer.stop();
	showStart();
}

void EmojiPan::otherLeave() {
	if (_removingSetId || s_inner.inlineResultsShown()) return;
	if (_a_appearance.animating()) {
		hideStart();
	} else {
		_hideTimer.start(0);
	}
}

void EmojiPan::mousePressEvent(QMouseEvent *e) {
	if (!_stickersShown) return;
	_iconsMousePos = e ? e->globalPos() : QCursor::pos();
	updateSelected();

	if (_iconOver == _icons.size()) {
		Ui::showLayer(new StickersBox());
	} else {
		_iconDown = _iconOver;
		_iconsMouseDown = _iconsMousePos;
		_iconsStartX = _iconsX.current();
	}
}

void EmojiPan::mouseMoveEvent(QMouseEvent *e) {
	if (!_stickersShown) return;
	_iconsMousePos = e ? e->globalPos() : QCursor::pos();
	updateSelected();

	int32 skip = 0;
	for (int32 i = 0, l = _icons.size(); i < l; ++i) {
		if (_icons.at(i).sticker) break;
		++skip;
	}
	if (!_iconsDragging && !_icons.isEmpty() && _iconDown >= skip) {
		if ((_iconsMousePos - _iconsMouseDown).manhattanLength() >= QApplication::startDragDistance()) {
			_iconsDragging = true;
		}
	}
	if (_iconsDragging) {
		int32 newX = snap(_iconsStartX + (rtl() ? -1 : 1) * (_iconsMouseDown.x() - _iconsMousePos.x()), 0, _iconsMax);
		if (newX != _iconsX.current()) {
			_iconsX = anim::ivalue(newX, newX);
			_iconsStartAnim = 0;
			if (_iconAnimations.isEmpty()) _a_icons.stop();
			updateIcons();
		}
	}
}

void EmojiPan::mouseReleaseEvent(QMouseEvent *e) {
	if (!_stickersShown || _icons.isEmpty()) return;

	int32 wasDown = _iconDown;
	_iconDown = -1;

	_iconsMousePos = e ? e->globalPos() : QCursor::pos();
	if (_iconsDragging) {
		int32 newX = snap(_iconsStartX + _iconsMouseDown.x() - _iconsMousePos.x(), 0, _iconsMax);
		if (newX != _iconsX.current()) {
			_iconsX = anim::ivalue(newX, newX);
			_iconsStartAnim = 0;
			if (_iconAnimations.isEmpty()) _a_icons.stop();
			updateIcons();
		}
		_iconsDragging = false;
		updateSelected();
	} else {
		updateSelected();

		if (wasDown == _iconOver && _iconOver >= 0 && _iconOver < _icons.size()) {
			_iconSelX = anim::ivalue(_iconOver * st::rbEmoji.width, _iconOver * st::rbEmoji.width);
			s_inner.showStickerSet(_icons.at(_iconOver).setId);
		}
	}
}

bool EmojiPan::event(QEvent *e) {
	if (e->type() == QEvent::TouchBegin) {

	} else if (e->type() == QEvent::Wheel) {
		int32 skip = 0;
		for (int32 i = 0, l = _icons.size(); i < l; ++i) {
			if (_icons.at(i).sticker) break;
			++skip;
		}
		if (!_icons.isEmpty() && _iconOver >= skip && _iconOver < _icons.size() && _iconDown < 0) {
			QWheelEvent *ev = static_cast<QWheelEvent*>(e);
			bool hor = (ev->angleDelta().x() != 0 || ev->orientation() == Qt::Horizontal);
			bool ver = (ev->angleDelta().y() != 0 || ev->orientation() == Qt::Vertical);
			if (hor) _horizontal = true;
			int32 newX = _iconsX.current();
			if (/*_horizontal && */hor) {
				newX = snap(newX - (rtl() ? -1 : 1) * (ev->pixelDelta().x() ? ev->pixelDelta().x() : ev->angleDelta().x()), 0, _iconsMax);
			} else if (/*!_horizontal && */ver) {
				newX = snap(newX - (ev->pixelDelta().y() ? ev->pixelDelta().y() : ev->angleDelta().y()), 0, _iconsMax);
			}
			if (newX != _iconsX.current()) {
				_iconsX = anim::ivalue(newX, newX);
				_iconsStartAnim = 0;
				if (_iconAnimations.isEmpty()) _a_icons.stop();
				updateSelected();
				updateIcons();
			}
		}
	}
	return TWidget::event(e);
}

void EmojiPan::fastHide() {
	if (_a_appearance.animating()) {
		_a_appearance.stop();
	}
	a_opacity = anim::fvalue(0, 0);
	_hideTimer.stop();
	hide();
	_cache = QPixmap();
}

void EmojiPan::refreshStickers() {
	s_inner.refreshStickers();
	if (!_stickersShown) {
		s_inner.preloadImages();
	}
}

void EmojiPan::refreshSavedGifs() {
	e_switch.updateText();
	e_switch.moveToRight(0, 0, st::emojiPanWidth);
	s_inner.refreshSavedGifs();
	if (!_stickersShown) {
		s_inner.preloadImages();
	}
}

void EmojiPan::onRefreshIcons() {
	_iconOver = -1;
	_iconHovers.clear();
	_iconAnimations.clear();
	s_inner.fillIcons(_icons);
	s_inner.fillPanels(s_panels);
	_iconsX = anim::ivalue(0, 0);
	_iconSelX.finish();
	_iconsStartAnim = 0;
	_a_icons.stop();
	if (_icons.isEmpty()) {
		_iconsMax = 0;
	} else {
		_iconHovers = QVector<float64>(_icons.size(), 0);
		_iconsMax = qMax(int((_icons.size() - 7) * st::rbEmoji.width), 0);
	}
	updatePanelsPositions(s_panels, s_scroll.scrollTop());
	updateSelected();
	if (_stickersShown) {
		validateSelectedIcon();
		updateContentHeight();
	}
	updateIcons();
}

void EmojiPan::onRefreshPanels() {
	s_inner.refreshPanels(s_panels);
	e_inner.refreshPanels(e_panels);
	if (_stickersShown) {
		updatePanelsPositions(s_panels, s_scroll.scrollTop());
	} else {
		updatePanelsPositions(e_panels, e_scroll.scrollTop());
	}
}

void EmojiPan::leaveToChildEvent(QEvent *e, QWidget *child) {
	if (!_stickersShown) return;
	_iconsMousePos = QCursor::pos();
	updateSelected();
}

void EmojiPan::updateSelected() {
	if (_iconDown >= 0) {
		return;
	}

	QPoint p(mapFromGlobal(_iconsMousePos));
	int32 x = p.x(), y = p.y(), newOver = -1;
	if (rtl()) x = width() - x;
	x -= _iconsLeft;
	if (x >= st::rbEmoji.width * 7 && x < st::rbEmoji.width * 8 && y >= _iconsTop && y < _iconsTop + st::rbEmoji.height) {
		newOver = _icons.size();
	} else if (!_icons.isEmpty()) {
		if (y >= _iconsTop && y < _iconsTop + st::rbEmoji.height && x >= 0 && x < 7 * st::rbEmoji.width && x < _icons.size() * st::rbEmoji.width) {
			int32 skip = 0;
			for (int32 i = 0, l = _icons.size(); i < l; ++i) {
				if (_icons.at(i).sticker) break;
				if (x < st::rbEmoji.width) {
					newOver = i;
					break;
				}
				x -= st::rbEmoji.width;
				++skip;
			}
			if (newOver < 0) {
				x += _iconsX.current();
				newOver = qFloor(x / st::rbEmoji.width) + skip;
			}
		}
	}
	if (newOver != _iconOver) {
		if (newOver < 0) {
			setCursor(style::cur_default);
		} else if (_iconOver < 0) {
			setCursor(style::cur_pointer);
		}
		bool startanim = false;
		if (_iconOver >= 0 && _iconOver < _icons.size()) {
			_iconAnimations.remove(_iconOver + 1);
			if (_iconAnimations.find(-_iconOver - 1) == _iconAnimations.end()) {
				if (_iconAnimations.isEmpty() && !_iconsStartAnim) startanim = true;
				_iconAnimations.insert(-_iconOver - 1, getms());
			}
		}
		_iconOver = newOver;
		if (_iconOver >= 0 && _iconOver < _icons.size()) {
			_iconAnimations.remove(-_iconOver - 1);
			if (_iconAnimations.find(_iconOver + 1) == _iconAnimations.end()) {
				if (_iconAnimations.isEmpty() && !_iconsStartAnim) startanim = true;
				_iconAnimations.insert(_iconOver + 1, getms());
			}
		}
		if (startanim && !_a_icons.animating()) _a_icons.start();
	}
}

void EmojiPan::updateIcons() {
	if (!_stickersShown || !s_inner.showSectionIcons()) return;

	QRect r(st::dropdownDef.padding.left(), st::dropdownDef.padding.top(), _width - st::dropdownDef.padding.left() - st::dropdownDef.padding.right(), _height - st::dropdownDef.padding.top() - st::dropdownDef.padding.bottom());
	update(r.left(), _iconsTop, r.width(), st::rbEmoji.height);
}

void EmojiPan::step_icons(uint64 ms, bool timer) {
	if (!_stickersShown) {
		_a_icons.stop();
		return;
	}

	for (Animations::iterator i = _iconAnimations.begin(); i != _iconAnimations.end();) {
		int index = qAbs(i.key()) - 1;
		float64 dt = float64(ms - i.value()) / st::emojiPanDuration;
		if (index >= _iconHovers.size()) {
			i = _iconAnimations.erase(i);
		} else if (dt >= 1) {
			_iconHovers[index] = (i.key() > 0) ? 1 : 0;
			i = _iconAnimations.erase(i);
		} else {
			_iconHovers[index] = (i.key() > 0) ? dt : (1 - dt);
			++i;
		}
	}

	if (_iconsStartAnim) {
		float64 dt = (ms - _iconsStartAnim) / st::stickerIconMove;
		if (dt >= 1) {
			_iconsStartAnim = 0;
			_iconsX.finish();
			_iconSelX.finish();
		} else {
			_iconsX.update(dt, anim::linear);
			_iconSelX.update(dt, anim::linear);
		}
		if (timer) updateSelected();
	}

	if (timer) updateIcons();

	if (_iconAnimations.isEmpty() && !_iconsStartAnim) {
		_a_icons.stop();
	}
}

void EmojiPan::step_slide(float64 ms, bool timer) {
	float64 fullDuration = st::introSlideDelta + st::introSlideDuration, dt = ms / fullDuration;
	float64 dt1 = (ms > st::introSlideDuration) ? 1 : (ms / st::introSlideDuration), dt2 = (ms > st::introSlideDelta) ? (ms - st::introSlideDelta) / (st::introSlideDuration) : 0;
	if (dt2 >= 1) {
		_a_slide.stop();
		a_fromCoord.finish();
		a_fromAlpha.finish();
		a_toCoord.finish();
		a_toAlpha.finish();
		_fromCache = _toCache = QPixmap();
		if (_cache.isNull()) showAll();
	} else {
		a_fromCoord.update(dt1, st::introHideFunc);
		a_fromAlpha.update(dt1, st::introAlphaHideFunc);
		a_toCoord.update(dt2, st::introShowFunc);
		a_toAlpha.update(dt2, st::introAlphaShowFunc);
	}
	if (timer) update();
}

void EmojiPan::step_appearance(float64 ms, bool timer) {
	if (_cache.isNull()) {
		_a_appearance.stop();
		return;
	}

	float64 dt = ms / st::dropdownDef.duration;
	if (dt >= 1) {
		_a_appearance.stop();
		a_opacity.finish();
		if (_hiding) {
			hideFinish();
		} else {
			_cache = QPixmap();
			if (_toCache.isNull()) showAll();
		}
	} else {
		a_opacity.update(dt, anim::linear);
	}
	if (timer) update();
}

void EmojiPan::hideStart() {
	if (_removingSetId || s_inner.inlineResultsShown()) return;

	hideAnimated();
}

void EmojiPan::prepareShowHideCache() {
	if (_cache.isNull()) {
		QPixmap from = _fromCache, to = _toCache;
		_fromCache = _toCache = QPixmap();
		showAll();
		_cache = myGrab(this, rect().marginsRemoved(st::dropdownDef.padding));
		_fromCache = from; _toCache = to;
	}
}

void EmojiPan::hideAnimated() {
	if (_hiding) return;

	prepareShowHideCache();
	hideAll();
	_hiding = true;
	a_opacity.start(0);
	_a_appearance.start();
}

void EmojiPan::hideFinish() {
	hide();
	e_inner.hideFinish();
	s_inner.hideFinish(true);
	_cache = _toCache = _fromCache = QPixmap();
	_a_slide.stop();
	_horizontal = false;
	_hiding = false;

	e_scroll.scrollToY(0);
	if (!_recent.checked()) {
		_noTabUpdate = true;
		_recent.setChecked(true);
		_noTabUpdate = false;
	}
	s_scroll.scrollToY(0);
	_iconOver = _iconDown = -1;
	_iconSel = 0;
	_iconsX = anim::ivalue(0, 0);
	_iconSelX = anim::ivalue(0, 0);
	_iconsStartAnim = 0;
	_a_icons.stop();
	_iconHovers = _icons.isEmpty() ? QVector<float64>() : QVector<float64>(_icons.size(), 0);
	_iconAnimations.clear();

	Notify::clipStopperHidden(ClipStopperSavedGifsPanel);
}

void EmojiPan::showStart() {
	if (!isHidden() && !_hiding) {
		return;
	}
	if (isHidden()) {
		e_inner.refreshRecent();
		if (s_inner.inlineResultsShown() && refreshInlineRows()) {
			_stickersShown = true;
			_shownFromInlineQuery = true;
		} else {
			s_inner.refreshRecent();
			_stickersShown = false;
			_shownFromInlineQuery = false;
			_cache = QPixmap(); // clear after refreshInlineRows()
		}
		recountContentMaxHeight();
		s_inner.preloadImages();
		_fromCache = _toCache = QPixmap();
		_a_slide.stop();
		moveBottom(y() + height(), true);
	} else if (_hiding) {
		if (s_inner.inlineResultsShown() && refreshInlineRows()) {
			onSwitch();
		}
	}
	prepareShowHideCache();
	hideAll();
	_hiding = false;
	show();
	a_opacity.start(1);
	_a_appearance.start();
	emit updateStickers();
}

bool EmojiPan::eventFilter(QObject *obj, QEvent *e) {
	if (e->type() == QEvent::Enter) {
		//if (dynamic_cast<StickerPan*>(obj)) {
		//	enterEvent(e);
		//} else {
			otherEnter();
		//}
	} else if (e->type() == QEvent::Leave) {
		//if (dynamic_cast<StickerPan*>(obj)) {
		//	leaveEvent(e);
		//} else {
			otherLeave();
		//}
	} else if (e->type() == QEvent::MouseButtonPress && static_cast<QMouseEvent*>(e)->button() == Qt::LeftButton/* && !dynamic_cast<StickerPan*>(obj)*/) {
		if (isHidden() || _hiding) {
			_hideTimer.stop();
			showStart();
		} else {
			hideAnimated();
		}
	}
	return false;
}

void EmojiPan::stickersInstalled(uint64 setId) {
	_stickersShown = true;
	if (isHidden()) {
		moveBottom(y() + height(), true);
		show();
		a_opacity = anim::fvalue(0, 1);
		a_opacity.update(0, anim::linear);
		_cache = _fromCache = _toCache = QPixmap();
	}
	showAll();
	s_inner.showStickerSet(setId);
	updateContentHeight();
	showStart();
}

void EmojiPan::notify_inlineItemLayoutChanged(const InlineBots::Layout::ItemBase *layout) {
	if (_stickersShown && !isHidden()) {
		s_inner.notify_inlineItemLayoutChanged(layout);
	}
}

void EmojiPan::ui_repaintInlineItem(const InlineBots::Layout::ItemBase *layout) {
	if (_stickersShown && !isHidden()) {
		s_inner.ui_repaintInlineItem(layout);
	}
}

bool EmojiPan::ui_isInlineItemVisible(const InlineBots::Layout::ItemBase *layout) {
	if (_stickersShown && !isHidden()) {
		return s_inner.ui_isInlineItemVisible(layout);
	}
	return false;
}

bool EmojiPan::ui_isInlineItemBeingChosen() {
	if (_stickersShown && !isHidden()) {
		return s_inner.ui_isInlineItemBeingChosen();
	}
	return false;
}

void EmojiPan::showAll() {
	if (_stickersShown) {
		s_scroll.show();
		_recent.hide();
		_people.hide();
		_nature.hide();
		_food.hide();
		_activity.hide();
		_travel.hide();
		_objects.hide();
		_symbols.hide();
		e_scroll.hide();
	} else {
		s_scroll.hide();
		_recent.show();
		_people.show();
		_nature.show();
		_food.show();
		_activity.show();
		_travel.show();
		_objects.show();
		_symbols.show();
		e_scroll.show();
	}
}

void EmojiPan::hideAll() {
	_recent.hide();
	_people.hide();
	_nature.hide();
	_food.hide();
	_activity.hide();
	_travel.hide();
	_objects.hide();
	_symbols.hide();
	e_scroll.hide();
	s_scroll.hide();
	e_inner.clearSelection(true);
	s_inner.clearSelection(true);
}

void EmojiPan::onTabChange() {
	if (_noTabUpdate) return;
	DBIEmojiTab newTab = dbietRecent;
	if (_people.checked()) newTab = dbietPeople;
	else if (_nature.checked()) newTab = dbietNature;
	else if (_food.checked()) newTab = dbietFood;
	else if (_activity.checked()) newTab = dbietActivity;
	else if (_travel.checked()) newTab = dbietTravel;
	else if (_objects.checked()) newTab = dbietObjects;
	else if (_symbols.checked()) newTab = dbietSymbols;
	e_inner.showEmojiPack(newTab);
}

void EmojiPan::updatePanelsPositions(const QVector<internal::EmojiPanel*> &panels, int32 st) {
	for (int32 i = 0, l = panels.size(); i < l; ++i) {
		int32 y = panels.at(i)->wantedY() - st;
		if (y < 0) {
			y = (i + 1 < l) ? qMin(panels.at(i + 1)->wantedY() - st - int(st::emojiPanHeader), 0) : 0;
		}
		panels.at(i)->move(0, y);
		panels.at(i)->setDeleteVisible(y >= st::emojiPanHeader);
	}
}

void EmojiPan::onScrollEmoji() {
	auto st = e_scroll.scrollTop();

	updatePanelsPositions(e_panels, st);

	auto tab = e_inner.currentTab(st);
	FlatRadiobutton *check = nullptr;
	switch (tab) {
	case dbietRecent: check = &_recent; break;
	case dbietPeople: check = &_people; break;
	case dbietNature: check = &_nature; break;
	case dbietFood: check = &_food; break;
	case dbietActivity: check = &_activity; break;
	case dbietTravel: check = &_travel; break;
	case dbietObjects: check = &_objects; break;
	case dbietSymbols: check = &_symbols; break;
	}
	if (check && !check->checked()) {
		_noTabUpdate = true;
		check->setChecked(true);
		_noTabUpdate = false;
	}

	e_inner.setScrollTop(st);
}

void EmojiPan::onScrollStickers() {
	auto st = s_scroll.scrollTop();

	updatePanelsPositions(s_panels, st);

	validateSelectedIcon(true);
	if (st + s_scroll.height() > s_scroll.scrollTopMax()) {
		onInlineRequest();
	}

	s_inner.setScrollTop(st);
}

void EmojiPan::validateSelectedIcon(bool animated) {
	uint64 setId = s_inner.currentSet(s_scroll.scrollTop());
	int32 newSel = 0;
	for (int32 i = 0, l = _icons.size(); i < l; ++i) {
		if (_icons.at(i).setId == setId) {
			newSel = i;
			break;
		}
	}
	if (newSel != _iconSel) {
		_iconSel = newSel;
		int32 skip = 0;
		for (int32 i = 0, l = _icons.size(); i < l; ++i) {
			if (_icons.at(i).sticker) break;
			++skip;
		}
		if (animated) {
			_iconSelX.start(newSel * st::rbEmoji.width);
		} else {
			_iconSelX = anim::ivalue(newSel * st::rbEmoji.width, newSel * st::rbEmoji.width);
		}
		_iconsX.start(snap((2 * newSel - 7 - skip) * int(st::rbEmoji.width) / 2, 0, _iconsMax));
		_iconsStartAnim = getms();
		_a_icons.start();
		updateSelected();
		updateIcons();
	}
}

void EmojiPan::onSwitch() {
	QPixmap cache = _cache;
	_fromCache = myGrab(this, rect().marginsRemoved(st::dropdownDef.padding));
	_stickersShown = !_stickersShown;
	if (!_stickersShown) {
		Notify::clipStopperHidden(ClipStopperSavedGifsPanel);
	} else {
		if (cShowingSavedGifs() && cSavedGifs().isEmpty()) {
			s_inner.showStickerSet(Stickers::DefaultSetId);
		} else if (!cShowingSavedGifs() && !cSavedGifs().isEmpty() && Global::StickerSets().isEmpty()) {
			s_inner.showStickerSet(Stickers::NoneSetId);
		} else {
			s_inner.updateShowingSavedGifs();
		}
		if (cShowingSavedGifs()) {
			s_inner.showFinish();
		}
		validateSelectedIcon();
		updateContentHeight();
	}
	_iconOver = -1;
	_iconHovers = _icons.isEmpty() ? QVector<float64>() : QVector<float64>(_icons.size(), 0);
	_iconAnimations.clear();
	_a_icons.stop();

	_cache = QPixmap();
	showAll();
	_toCache = myGrab(this, rect().marginsRemoved(st::dropdownDef.padding));
	_cache = cache;

	hideAll();

	if (_stickersShown) {
		e_inner.hideFinish();
	} else {
		s_inner.hideFinish(false);
	}

	a_toCoord = (_stickersShown != rtl()) ? anim::ivalue(st::emojiPanWidth, 0) : anim::ivalue(-st::emojiPanWidth, 0);
	a_toAlpha = anim::fvalue(0, 1);
	a_fromCoord = (_stickersShown != rtl()) ? anim::ivalue(0, -st::emojiPanWidth) : anim::ivalue(0, st::emojiPanWidth);
	a_fromAlpha = anim::fvalue(1, 0);

	_a_slide.start();
	update();
}

void EmojiPan::onRemoveSet(quint64 setId) {
	auto it = Global::StickerSets().constFind(setId);
	if (it != Global::StickerSets().cend() && !(it->flags & MTPDstickerSet::Flag::f_official)) {
		_removingSetId = it->id;
		ConfirmBox *box = new ConfirmBox(lng_stickers_remove_pack(lt_sticker_pack, it->title), lang(lng_box_remove));
		connect(box, SIGNAL(confirmed()), this, SLOT(onRemoveSetSure()));
		connect(box, SIGNAL(destroyed(QObject*)), this, SLOT(onDelayedHide()));
		Ui::showLayer(box);
	}
}

void EmojiPan::onRemoveSetSure() {
	Ui::hideLayer();
	auto it = Global::RefStickerSets().find(_removingSetId);
	if (it != Global::RefStickerSets().cend() && !(it->flags & MTPDstickerSet::Flag::f_official)) {
		if (it->id && it->access) {
			MTP::send(MTPmessages_UninstallStickerSet(MTP_inputStickerSetID(MTP_long(it->id), MTP_long(it->access))));
		} else if (!it->shortName.isEmpty()) {
			MTP::send(MTPmessages_UninstallStickerSet(MTP_inputStickerSetShortName(MTP_string(it->shortName))));
		}
		bool writeRecent = false;
		RecentStickerPack &recent(cGetRecentStickers());
		for (RecentStickerPack::iterator i = recent.begin(); i != recent.cend();) {
			if (it->stickers.indexOf(i->first) >= 0) {
				i = recent.erase(i);
				writeRecent = true;
			} else {
				++i;
			}
		}
		Global::RefStickerSets().erase(it);
		int removeIndex = Global::StickerSetsOrder().indexOf(_removingSetId);
		if (removeIndex >= 0) Global::RefStickerSetsOrder().removeAt(removeIndex);
		refreshStickers();
		Local::writeStickers();
		if (writeRecent) Local::writeUserSettings();
	}
	_removingSetId = 0;
}

void EmojiPan::onDelayedHide() {
	if (!rect().contains(mapFromGlobal(QCursor::pos()))) {
		_hideTimer.start(3000);
	}
	_removingSetId = 0;
}

void EmojiPan::clearInlineBot() {
	inlineBotChanged();
	e_switch.updateText();
	e_switch.moveToRight(0, 0, st::emojiPanWidth);
}

bool EmojiPan::hideOnNoInlineResults() {
	return _inlineBot && _stickersShown && s_inner.inlineResultsShown() && (_shownFromInlineQuery || _inlineBot->username != cInlineGifBotUsername());
}

void EmojiPan::inlineBotChanged() {
	if (!_inlineBot) return;

	if (!isHidden() && !_hiding) {
		if (hideOnNoInlineResults() || !rect().contains(mapFromGlobal(QCursor::pos()))) {
			hideAnimated();
		}
	}

	if (_inlineRequestId) MTP::cancel(_inlineRequestId);
	_inlineRequestId = 0;
	_inlineQuery = _inlineNextQuery = _inlineNextOffset = QString();
	_inlineBot = nullptr;
	for (InlineCache::const_iterator i = _inlineCache.cbegin(), e = _inlineCache.cend(); i != e; ++i) {
		delete i.value();
	}
	_inlineCache.clear();
	s_inner.inlineBotChanged();
	s_inner.hideInlineRowsPanel();

	Notify::inlineBotRequesting(false);
}

void EmojiPan::inlineResultsDone(const MTPmessages_BotResults &result) {
	_inlineRequestId = 0;
	Notify::inlineBotRequesting(false);

	auto it = _inlineCache.find(_inlineQuery);

	bool adding = (it != _inlineCache.cend());
	if (result.type() == mtpc_messages_botResults) {
		const auto &d(result.c_messages_botResults());
		const auto &v(d.vresults.c_vector().v);
		uint64 queryId(d.vquery_id.v);

		if (!adding) {
			it = _inlineCache.insert(_inlineQuery, new internal::InlineCacheEntry());
		}
		it.value()->nextOffset = qs(d.vnext_offset);
		if (d.has_switch_pm() && d.vswitch_pm.type() == mtpc_inlineBotSwitchPM) {
			const auto &switchPm = d.vswitch_pm.c_inlineBotSwitchPM();
			it.value()->switchPmText = qs(switchPm.vtext);
			it.value()->switchPmStartToken = qs(switchPm.vstart_param);
		}

		if (int count = v.size()) {
			it.value()->results.reserve(it.value()->results.size() + count);
		}
		int added = 0;
		for_const (const auto &res, v) {
			if (auto result = InlineBots::Result::create(queryId, res)) {
				++added;
				it.value()->results.push_back(result.release());
			}
		}

		if (!added) {
			it.value()->nextOffset = QString();
		}
	} else if (adding) {
		it.value()->nextOffset = QString();
	}

	if (!showInlineRows(!adding)) {
		it.value()->nextOffset = QString();
	}
	onScrollStickers();
}

bool EmojiPan::inlineResultsFail(const RPCError &error) {
	// show error?
	Notify::inlineBotRequesting(false);
	_inlineRequestId = 0;
	return true;
}

void EmojiPan::queryInlineBot(UserData *bot, PeerData *peer, QString query) {
	bool force = false;
	_inlineQueryPeer = peer;
	if (bot != _inlineBot) {
		inlineBotChanged();
		_inlineBot = bot;
		force = true;
		//if (_inlineBot->isBotInlineGeo()) {
		//	Ui::showLayer(new InformBox(lang(lng_bot_inline_geo_unavailable)));
		//}
	}
	//if (_inlineBot && _inlineBot->isBotInlineGeo()) {
	//	return;
	//}

	if (_inlineQuery != query || force) {
		if (_inlineRequestId) {
			MTP::cancel(_inlineRequestId);
			_inlineRequestId = 0;
			Notify::inlineBotRequesting(false);
		}
		if (_inlineCache.contains(query)) {
			_inlineRequestTimer.stop();
			_inlineQuery = _inlineNextQuery = query;
			showInlineRows(true);
		} else {
			_inlineNextQuery = query;
			_inlineRequestTimer.start(InlineBotRequestDelay);
		}
	}
}

void EmojiPan::onInlineRequest() {
	if (_inlineRequestId || !_inlineBot || !_inlineQueryPeer) return;
	_inlineQuery = _inlineNextQuery;

	QString nextOffset;
	InlineCache::const_iterator i = _inlineCache.constFind(_inlineQuery);
	if (i != _inlineCache.cend()) {
		nextOffset = i.value()->nextOffset;
		if (nextOffset.isEmpty()) return;
	}
	Notify::inlineBotRequesting(true);
	MTPmessages_GetInlineBotResults::Flags flags = 0;
	_inlineRequestId = MTP::send(MTPmessages_GetInlineBotResults(MTP_flags(flags), _inlineBot->inputUser, _inlineQueryPeer->input, MTPInputGeoPoint(), MTP_string(_inlineQuery), MTP_string(nextOffset)), rpcDone(&EmojiPan::inlineResultsDone), rpcFail(&EmojiPan::inlineResultsFail));
}

void EmojiPan::onEmptyInlineRows() {
	if (_shownFromInlineQuery || hideOnNoInlineResults()) {
		hideAnimated();
		s_inner.clearInlineRowsPanel();
	} else if (!_inlineBot) {
		s_inner.hideInlineRowsPanel();
	} else {
		s_inner.clearInlineRowsPanel();
	}
}

bool EmojiPan::refreshInlineRows(int32 *added) {
	auto i = _inlineCache.constFind(_inlineQuery);
	const internal::InlineCacheEntry *entry = nullptr;
	if (i != _inlineCache.cend()) {
		if (!i.value()->results.isEmpty() || !i.value()->switchPmText.isEmpty()) {
			entry = i.value();
		}
		_inlineNextOffset = i.value()->nextOffset;
	}
	if (!entry) prepareShowHideCache();
	int32 result = s_inner.refreshInlineRows(_inlineBot, entry, false);
	if (added) *added = result;
	return (entry != nullptr);
}

int32 EmojiPan::showInlineRows(bool newResults) {
	int32 added = 0;
	bool clear = !refreshInlineRows(&added);
	if (newResults) s_scroll.scrollToY(0);

	e_switch.updateText(s_inner.inlineResultsShown() ? _inlineBot->username : QString());
	e_switch.moveToRight(0, 0, st::emojiPanWidth);

	bool hidden = isHidden();
	if (!hidden && !clear) {
		recountContentMaxHeight();
	}
	if (clear) {
		if (!hidden && hideOnNoInlineResults()) {
			hideAnimated();
		} else if (!_hiding) {
			_cache = QPixmap(); // clear after refreshInlineRows()
		}
	} else {
		_hideTimer.stop();
		if (hidden || _hiding) {
			showStart();
		} else if (!_stickersShown) {
			onSwitch();
		}
	}

	return added;
}

void EmojiPan::recountContentMaxHeight() {
	if (_shownFromInlineQuery) {
		_contentMaxHeight = qMin(s_inner.countHeight(true), int(st::emojiPanMaxHeight));
	} else {
		_contentMaxHeight = st::emojiPanMaxHeight;
	}
	updateContentHeight();
}
