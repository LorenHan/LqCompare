#ifndef LQCOMPARE_PICTURECOMPAREVIEW_H
#define LQCOMPARE_PICTURECOMPAREVIEW_H

#include <QWidget>
#include <QPointer>

class QComboBox;
class QCheckBox;
class QDoubleSpinBox;
class QLabel;
class QLineEdit;
class QPushButton;
class QScrollArea;
class QSlider;
class QSpinBox;
class QStackedWidget;

namespace LqCompare {
class PictureCompareSession;
class PictureCanvas;

class PictureCompareView : public QWidget {
    Q_OBJECT
public:
    explicit PictureCompareView(PictureCompareSession *session, QWidget *parent = nullptr);
    double zoomFactor() const;
public slots:
    void setZoomFactor(double zoom);
    void fitToWindow();
    void focusDifference(int index);
private:
    void refresh();
    void openPaths();
    void updateOptions();
    void updateNavigation(int index);
    QPointer<PictureCompareSession> m_session;
    QLineEdit *m_leftPath;
    QLineEdit *m_rightPath;
    QLabel *m_leftInfo;
    QLabel *m_rightInfo;
    QLabel *m_status;
    QLabel *m_legend;
    QComboBox *m_mode;
    QComboBox *m_alpha;
    QComboBox *m_connectivity;
    QComboBox *m_regions;
    QCheckBox *m_regionBoxes;
    QPushButton *m_firstRegion;
    QPushButton *m_previousRegion;
    QPushButton *m_nextRegion;
    QPushButton *m_lastRegion;
    QSpinBox *m_tolerance;
    QDoubleSpinBox *m_zoom;
    QSlider *m_opacity;
    QStackedWidget *m_stack;
    QScrollArea *m_leftScroll;
    QScrollArea *m_rightScroll;
    QScrollArea *m_compositeScroll;
    PictureCanvas *m_leftCanvas;
    PictureCanvas *m_rightCanvas;
    PictureCanvas *m_compositeCanvas;
    bool m_syncing = false;
};
}
#endif
