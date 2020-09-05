/***************************************************************************
                        amarokslider.cpp  -  description
                           -------------------
  begin                : Dec 15 2003
  copyright            : (C) 2003 by Mark Kretschmann
  email                : markey@web.de
  copyright            : (C) 2005 by Gábor Lehel
  email                : illissius@gmail.com
 ***************************************************************************/

/***************************************************************************
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 2 of the License, or     *
 *   (at your option) any later version.                                   *
 *                                                                         *
 ***************************************************************************/

#include "config.h"

#include "volumeslider.h"

#include <QApplication>
#include <QWidget>
#include <QMap>
#include <QString>
#include <QStringBuilder>
#include <QImage>
#include <QPainter>
#include <QPainterPath>
#include <QPalette>
#include <QFont>
#include <QBrush>
#include <QPen>
#include <QPoint>
#include <QPolygon>
#include <QRect>
#include <QVector>
#include <QMenu>
#include <QStyle>
#include <QStyleOption>
#include <QTimer>
#include <QAction>
#include <QSlider>
#include <QLinearGradient>
#include <QStyleOptionViewItem>
#include <QFlags>
#include <QtEvents>

SliderSlider::SliderSlider(Qt::Orientation orientation, QWidget* parent, uint max)
    : QSlider(orientation, parent),
      m_sliding(false),
      m_outside(false),
      m_prevValue(0) {
  setRange(0, max);
}

void SliderSlider::wheelEvent(QWheelEvent* e) {

  if (orientation() == Qt::Vertical) {
    // Will be handled by the parent widget
    e->ignore();
    return;
  }

  // Position Slider (horizontal)
  int step = e->angleDelta().y() * 1500 / 18;
  int nval = qBound(minimum(), QSlider::value() + step, maximum());

  QSlider::setValue(nval);

  emit sliderReleased(value());

}

void SliderSlider::mouseMoveEvent(QMouseEvent *e) {

  if (m_sliding) {
    // feels better, but using set value of 20 is bad of course
    QRect rect(-20, -20, width() + 40, height() + 40);

    if (orientation() == Qt::Horizontal && !rect.contains(e->pos())) {
      if (!m_outside) QSlider::setValue(m_prevValue);
      m_outside = true;
    }
    else {
      m_outside = false;
      slideEvent(e);
      emit sliderMoved(value());
    }
  }
  else
    QSlider::mouseMoveEvent(e);

}

void SliderSlider::slideEvent(QMouseEvent* e) {

  QStyleOptionSlider option;
  initStyleOption(&option);
  QRect sliderRect(style()->subControlRect(QStyle::CC_Slider, &option, QStyle::SC_SliderHandle, this));

  QSlider::setValue(
      orientation() == Qt::Horizontal
          ? ((QApplication::layoutDirection() == Qt::RightToLeft)
                 ? QStyle::sliderValueFromPosition(
                       minimum(), maximum(),
                       width() - (e->pos().x() - sliderRect.width() / 2),
                       width() + sliderRect.width(), true)
                 : QStyle::sliderValueFromPosition(
                       minimum(), maximum(),
                       e->pos().x() - sliderRect.width() / 2,
                       width() - sliderRect.width()))
          : QStyle::sliderValueFromPosition(
                minimum(), maximum(), e->pos().y() - sliderRect.height() / 2,
                height() - sliderRect.height()));

}

void SliderSlider::mousePressEvent(QMouseEvent* e) {

  QStyleOptionSlider option;
  initStyleOption(&option);
  QRect sliderRect(style()->subControlRect(QStyle::CC_Slider, &option, QStyle::SC_SliderHandle, this));

  m_sliding = true;
  m_prevValue = QSlider::value();

  if (!sliderRect.contains(e->pos())) mouseMoveEvent(e);

}

void SliderSlider::mouseReleaseEvent(QMouseEvent*) {

  if (!m_outside && QSlider::value() != m_prevValue)
    emit sliderReleased(value());

  m_sliding = false;
  m_outside = false;

}

void SliderSlider::setValue(int newValue) {
  // don't adjust the slider while the user is dragging it!

  if (!m_sliding || m_outside)
    QSlider::setValue(adjustValue(newValue));
  else
    m_prevValue = newValue;
}

//////////////////////////////////////////////////////////////////////////////////////////
/// CLASS PrettySlider
//////////////////////////////////////////////////////////////////////////////////////////

#define THICKNESS 7
#define MARGIN 3

PrettySlider::PrettySlider(Qt::Orientation orientation, SliderMode mode, QWidget* parent, uint max)
    : SliderSlider(orientation, parent, max), m_mode(mode) {
  if (m_mode == Pretty) {
    setFocusPolicy(Qt::NoFocus);
  }
}

void PrettySlider::mousePressEvent(QMouseEvent* e) {
  SliderSlider::mousePressEvent(e);

  slideEvent(e);
}

void PrettySlider::slideEvent(QMouseEvent* e) {
  if (m_mode == Pretty)
    QSlider::setValue(
        orientation() == Qt::Horizontal
            ? QStyle::sliderValueFromPosition(minimum(), maximum(), e->pos().x(), width() - 2)
            : QStyle::sliderValueFromPosition(minimum(), maximum(), e->pos().y(), height() - 2));
  else
    SliderSlider::slideEvent(e);
}

namespace Amarok {
namespace ColorScheme {
extern QColor Background;
extern QColor Foreground;
}
}

#if 0
/** these functions aren't required in our fixed size world, but they may become useful one day **/

QSize PrettySlider::minimumSizeHint() const {
    return sizeHint();
}

QSize PrettySlider::sizeHint() const {
    constPolish();

    return (orientation() == Horizontal
             ? QSize( maxValue(), THICKNESS + MARGIN )
             : QSize( THICKNESS + MARGIN, maxValue() )).expandedTo( QApplit ication::globalStrut() );
}
#endif

//////////////////////////////////////////////////////////////////////////////////////////
/// CLASS VolumeSlider
//////////////////////////////////////////////////////////////////////////////////////////

VolumeSlider::VolumeSlider(QWidget* parent, uint max)
    : SliderSlider(Qt::Horizontal, parent, max),
      m_animCount(0),
      m_animTimer(new QTimer(this)),
      m_pixmapInset(QPixmap(drawVolumePixmap ())) {
  setFocusPolicy(Qt::NoFocus);

  // Store theme colors to check theme change at paintEvent
  m_previous_theme_text_color = palette().color(QPalette::WindowText);
  m_previous_theme_highlight_color = palette().color(QPalette::Highlight);

  drawVolumeSliderHandle();
  generateGradient();

  setMinimumWidth(m_pixmapInset.width());
  setMinimumHeight(m_pixmapInset.height());

  connect(m_animTimer, SIGNAL(timeout()), this, SLOT(slotAnimTimer()));

}

void VolumeSlider::SetEnabled(const bool enabled) {
  QSlider::setEnabled(enabled);
  QSlider::setVisible(enabled);
}

void VolumeSlider::generateGradient() {

  const QImage mask(":/pictures/volumeslider-gradient.png");

  QImage gradient_image(mask.size(), QImage::Format_ARGB32_Premultiplied);
  QPainter p(&gradient_image);

  QLinearGradient gradient(gradient_image.rect().topLeft(), gradient_image.rect().topRight());
  gradient.setColorAt(0, palette().color(QPalette::Window));
  gradient.setColorAt(1, palette().color(QPalette::Highlight));
  p.fillRect(gradient_image.rect(), QBrush(gradient));

  p.setCompositionMode(QPainter::CompositionMode_DestinationIn);
  p.drawImage(0, 0, mask);
  p.end();

  m_pixmapGradient = QPixmap::fromImage(gradient_image);

}

void VolumeSlider::slotAnimTimer() {

  if (m_animEnter) {
    m_animCount++;
    update();
    if (m_animCount == ANIM_MAX - 1) m_animTimer->stop();
  }
  else {
    m_animCount--;
    update();
    if (m_animCount == 0) m_animTimer->stop();
  }

}

void VolumeSlider::mousePressEvent(QMouseEvent* e) {

  if (e->button() != Qt::RightButton) {
    SliderSlider::mousePressEvent(e);
    slideEvent(e);
  }

}

void VolumeSlider::contextMenuEvent(QContextMenuEvent* e) {

  QMap<QAction*, int> values;
  QMenu menu;
  menu.setTitle("Volume");
  values[menu.addAction("100%")] = 100;
  values[menu.addAction("80%")] = 80;
  values[menu.addAction("60%")] = 60;
  values[menu.addAction("40%")] = 40;
  values[menu.addAction("20%")] = 20;
  values[menu.addAction("0%")] = 0;

  QAction* ret = menu.exec(mapToGlobal(e->pos()));
  if (ret) {
    QSlider::setValue(values[ret]);
    emit sliderReleased(values[ret]);
  }

}

void VolumeSlider::slideEvent(QMouseEvent* e) {
  QSlider::setValue(QStyle::sliderValueFromPosition(minimum(), maximum(), e->pos().x(), width() - 2));
}

void VolumeSlider::wheelEvent(QWheelEvent* e) {

  const uint step = e->angleDelta().y() / (e->angleDelta().x() == 0 ? 30 : -30);
  QSlider::setValue(QSlider::value() + step);
  emit sliderReleased(value());

}

void VolumeSlider::paintEvent(QPaintEvent*) {

  QPainter p(this);

  const int padding = 7;
  const int offset = int(double((width() - 2 * padding) * value()) / maximum());

  // If theme changed since last paintEvent, redraw the volume pixmap with new theme colors
  if (m_previous_theme_text_color != palette().color(QPalette::WindowText)) {
    m_pixmapInset = drawVolumePixmap();
    m_previous_theme_text_color = palette().color(QPalette::WindowText);
  }

  if (m_previous_theme_highlight_color != palette().color(QPalette::Highlight)) {
    drawVolumeSliderHandle();
    m_previous_theme_highlight_color = palette().color(QPalette::Highlight);
  }

  p.drawPixmap(0, 0, m_pixmapGradient, 0, 0, offset + padding, 0);
  p.drawPixmap(0, 0, m_pixmapInset);
  p.drawPixmap(offset - m_handlePixmaps[0].width() / 2 + padding, 0, m_handlePixmaps[m_animCount]);

  // Draw percentage number
  QStyleOptionViewItem opt;
  p.setPen(opt.palette.color(QPalette::Normal, QPalette::Text));
  QFont vol_font(opt.font);
  vol_font.setPixelSize(9);
  p.setFont(vol_font);
  const QRect rect(0, 0, 34, 15);
  p.drawText(rect, Qt::AlignRight | Qt::AlignVCenter, QString::number(value()) + '%');

}

#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
void VolumeSlider::enterEvent(QEnterEvent*) {
#else
void VolumeSlider::enterEvent(QEvent*) {
#endif

  m_animEnter = true;
  m_animCount = 0;

  m_animTimer->start(ANIM_INTERVAL);

}

void VolumeSlider::leaveEvent(QEvent*) {

  // This can happen if you enter and leave the widget quickly
  if (m_animCount == 0) m_animCount = 1;

  m_animEnter = false;
  m_animTimer->start(ANIM_INTERVAL);

}

void VolumeSlider::paletteChange(const QPalette&) {
  generateGradient();
}

QPixmap VolumeSlider::drawVolumePixmap () const {

  QPixmap pixmap(112, 36);
  pixmap.fill(Qt::transparent);
  QPainter painter(&pixmap);
  QPen pen(palette().color(QPalette::WindowText), 0.3, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
  painter.setPen(pen);

  painter.setRenderHint(QPainter::Antialiasing);
  painter.setRenderHint(QPainter::SmoothPixmapTransform);
  // Draw volume control pixmap
  QPolygon poly;
  poly << QPoint(6, 21) << QPoint(104, 21) << QPoint(104, 7) << QPoint(6, 16) << QPoint(6, 21);
  QPainterPath path;
  path.addPolygon(poly);
  painter.drawPolygon(poly);
  painter.drawLine(6, 29, 104, 29);
  // Return QPixmap
  return pixmap;

}

void VolumeSlider::drawVolumeSliderHandle() {

  QImage pixmapHandle(":/pictures/volumeslider-handle.png");
  QImage pixmapHandleGlow(":/pictures/volumeslider-handle_glow.png");

  QImage pixmapHandleGlow_image(pixmapHandleGlow.size(), QImage::Format_ARGB32_Premultiplied);
  QPainter painter(&pixmapHandleGlow_image);

  painter.setRenderHint(QPainter::Antialiasing);
  painter.setRenderHint(QPainter::SmoothPixmapTransform);

  // repaint volume slider handle glow image with theme highlight color
  painter.fillRect(pixmapHandleGlow_image.rect(), QBrush(palette().color(QPalette::Highlight)));
  painter.setCompositionMode(QPainter::CompositionMode_DestinationIn);
  painter.drawImage(0, 0, pixmapHandleGlow);

  // Overlay the volume slider handle image
  painter.setCompositionMode(QPainter::CompositionMode_SourceAtop);
  painter.drawImage(0, 0, pixmapHandle);

  // BEGIN Calculate handle animation pixmaps for mouse-over effect
  float opacity = 0.0;
  const float step = 1.0 / ANIM_MAX;
  QImage dst;
  m_handlePixmaps.clear();
  for (int i = 0; i < ANIM_MAX; ++i) {
    dst = pixmapHandle.copy();

    QPainter p(&dst);
    p.setOpacity(opacity);
    p.drawImage(0, 0, pixmapHandleGlow_image);
    p.end();

    m_handlePixmaps.append(QPixmap::fromImage(dst));
    opacity += step;
  }
  // END

}
