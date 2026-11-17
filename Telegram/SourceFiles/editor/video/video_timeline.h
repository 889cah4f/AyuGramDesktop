/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/timer.h"
#include "base/weak_ptr.h"
#include "ui/effects/animations.h"
#include "ui/rp_widget.h"

namespace Editor {

struct VideoTimelineDescriptor {
	QString path;
	QByteArray content;
	QSize dimensions;
	crl::time duration = 0;

	crl::time maxDuration = 0;
	crl::time minDuration = 0;

	// A zero |till| means the whole allowed window is selected.
	crl::time from = 0;
	crl::time till = 0;
	crl::time cover = 0;
};

class VideoTimeline final
	: public Ui::RpWidget
	, public base::has_weak_ptr {
public:
	VideoTimeline(
		not_null<Ui::RpWidget*> parent,
		VideoTimelineDescriptor descriptor);
	~VideoTimeline();

	[[nodiscard]] crl::time from() const {
		return _from;
	}
	[[nodiscard]] crl::time till() const {
		return _till;
	}
	[[nodiscard]] crl::time cover() const {
		return _cover;
	}

	[[nodiscard]] rpl::producer<crl::time> trimChanges() const {
		return _trimChanges.events();
	}
	[[nodiscard]] rpl::producer<crl::time> coverChanges() const {
		return _coverChanges.events();
	}
	[[nodiscard]] rpl::producer<bool> draggingChanges() const {
		return _draggingChanges.events();
	}
	[[nodiscard]] auto zoomChanges() const
	-> rpl::producer<std::pair<crl::time, crl::time>> {
		return _zoomChanges.events();
	}

	void setPlaybackPosition(crl::time position);

	void setSizeLabel(const QString &text);

	[[nodiscard]] QPoint coverDot() const;

	[[nodiscard]] bool draggingHead() const;
	[[nodiscard]] crl::time currentGrabTime() const {
		return grabbedTime();
	}

	[[nodiscard]] int resizeGetHeight(int newWidth) override;

private:
	enum class Grab {
		None,
		Left,
		Right,
		Head,

		Window,
	};

	void paintEvent(QPaintEvent *e) override;
	void mousePressEvent(QMouseEvent *e) override;
	void mouseMoveEvent(QMouseEvent *e) override;
	void mouseReleaseEvent(QMouseEvent *e) override;
	void leaveEventHook(QEvent *e) override;

	[[nodiscard]] QRect stripRect() const;
	[[nodiscard]] QRect labelRect() const;
	[[nodiscard]] crl::time minSelection() const;
	void moveWindowTo(crl::time center);
	[[nodiscard]] crl::time timeAt(int x) const;
	[[nodiscard]] int xAt(crl::time time) const;
	[[nodiscard]] std::pair<crl::time, crl::time> currentView() const;
	[[nodiscard]] int xAtView(
		std::pair<crl::time, crl::time> view,
		crl::time time) const;
	[[nodiscard]] crl::time grabbedTime() const;
	[[nodiscard]] bool zoomed() const;
	void animateViewTo(crl::time from, crl::time till);
	void restartHold(QPoint position);
	void zoomIn();
	void zoomOut();
	void panStep();
	[[nodiscard]] Grab grabAt(QPoint position) const;

	void applyGrab(QPoint position);
	void setCover(crl::time cover, bool notify);
	void updateCursor(Grab grab);
	void reloadFrames();
	void paintFrames(QPainter &p, const QRect &strip);
	void paintSelection(QPainter &p, const QRect &strip);
	void paintHead(QPainter &p, const QRect &strip);
	void paintCoverDot(QPainter &p);
	void paintDuration(QPainter &p, const QRect &strip);

	const VideoTimelineDescriptor _descriptor;
	const crl::time _duration = 0;
	const crl::time _maxDuration = 0;
	const crl::time _minDuration = 0;

	crl::time _from = 0;
	crl::time _till = 0;
	crl::time _cover = 0;
	// Negative means nothing played yet; zero is a real position.
	crl::time _playback = -1;

	QString _sizeLabel;

	std::vector<QImage> _frames;
	QSize _framesBox;
	int _frameWidth = 0;
	std::shared_ptr<std::atomic<bool>> _framesCancel;

	Grab _grab = Grab::None;
	int _grabShift = 0;

	crl::time _viewFrom = 0;
	crl::time _viewTill = 0;
	crl::time _viewFromStart = 0;
	crl::time _viewTillStart = 0;
	Ui::Animations::Simple _viewAnimation;
	base::Timer _holdTimer;
	base::Timer _panTimer;
	QPoint _holdAnchor;
	QPoint _lastPosition;
	int _zoomStage = 0;
	Ui::Animations::Simple _dotActive;

	rpl::event_stream<crl::time> _trimChanges;
	rpl::event_stream<crl::time> _coverChanges;
	rpl::event_stream<bool> _draggingChanges;
	rpl::event_stream<std::pair<crl::time, crl::time>> _zoomChanges;

};

} // namespace Editor
