/*
 * stress_test_qt.cpp
 * ---------------------------------------------------------------
 * Scripted UI interaction stress test for the Qt dashboard.
 *
 * WHY THIS EXISTS / HOW TO READ THIS FILE
 * ---------------------------------------------------------------
 * This targets the actual PerfTestWindow from the provided Qt main.cpp
 * (Qt 5, no Q_OBJECT / no MOC, old-style SIGNAL/SLOT connect syntax).
 * It uses REAL Qt synthetic-input API (QTest::mouseClick, QTest::mouseMove,
 * QTest::mousePress/mouseRelease from <QtTest/QtTest>) rather than
 * hand-rolled event posting, because QTest's helpers already handle the
 * widget-under-point resolution, button state tracking, and event-loop
 * pumping that raw QCoreApplication::postEvent would require you to
 * reimplement by hand. Text fields are populated via setText() /
 * setPlainText() rather than QTest::keyClicks() -- see Step 7 below.
 *
 * IMPORTANT CAVEAT -- PLEASE READ BEFORE BUILDING
 * ---------------------------------------------------------------
 * This sandbox has Qt5 *runtime* libraries installed (libQt5Widgets.so,
 * libQt5Gui.so, libQt5Core.so) but NOT the Qt5 *development* package
 * (no <QApplication> headers, no qmake, no Qt5Widgets.pc for pkg-config).
 * I could not compile or run this file here -- there is nothing to test
 * it against. This file is written to compile and run on a normal Qt5
 * dev machine; see BUILD instructions below for exactly what's needed.
 *
 * WHAT IT ACTUALLY DOES
 * ---------------------------------------------------------------
 * Subclasses PerfTestWindow (same class, same setupUI(), same
 * stylesheet, same paintEvent-based frame counter) and adds a
 * scripted, deterministic interaction sequence run once via a
 * single-shot QTimer after the event loop starts:
 *
 *   1. Click "Primary Action", "Secondary", "Cancel"
 *      (NOTE: original main.cpp wires btnPrimary's clicked() signal
 *      to qApp->quit() via old-style connect. That's preserved
 *      behavior from your file, so clicking Primary Action WILL
 *      close the window immediately, same as it would for a real
 *      user. If you want the script to reach Steps 2-9, comment out
 *      that one connect() in PerfTestWindow::setupUI(), OR run this
 *      test's Step 1 with Primary Action LAST rather than first --
 *      both options are given below, pick one before building.)
 *   2. Toggle checkboxes A, B, C
 *   3. Drag the Volume slider left->right in steps
 *   4. Drag the Brightness slider right->left in steps
 *   5. Open the dropdown, select each of the 5 options
 *   6. Flip both toggle-style QCheckBox "switches"
 *   7. Click to focus, then set text on name/email/message fields
 *      (no per-character key events -- see the note above Step 7 in
 *      runStressScript() for why)
 *   8. Click every row in the QListWidget
 *   9. Click Save, Reset, Export Data
 *
 * After the script completes, it calls qApp->quit() itself -- this is
 * a one-shot smoke/perf test, not an infinite hammer.
 *
 * FPS accounting mirrors your original main.cpp's setupFPSCounter():
 * paintEvent() increments frameCount, a 1s QTimer prints FPS, PLUS
 * (since this run is finite, not infinite) a final summary of total
 * frames / total wall time / min-max observed per-second FPS.
 *
 * BUILD (on a machine with full Qt5 dev headers + qmake, e.g. after
 *   `sudo apt-get install qtbase5-dev qttools5-dev qttools5-dev-tools`
 *   on Debian/Ubuntu, or the equivalent for your platform):
 *
 *   qmake -project QT+=widgets\ testlib
 *   # or write a .pro by hand:
 *   #   QT += widgets testlib
 *   #   CONFIG += console c++14
 *   #   SOURCES += stress_test_qt.cpp
 *   qmake && make
 *   ./stress_test_qt
 *
 * Or directly with g++ (adjust include/lib paths for your system,
 * e.g. via `pkg-config --cflags --libs Qt5Widgets Qt5Test`):
 *
 *   g++ -std=c++14 -fPIC $(pkg-config --cflags Qt5Widgets Qt5Test) \
 *       stress_test_qt.cpp -o stress_test_qt \
 *       $(pkg-config --libs Qt5Widgets Qt5Test)
 */

#include <QApplication>
#include <QWidget>
#include <QPushButton>
#include <QLabel>
#include <QProgressBar>
#include <QCheckBox>
#include <QSlider>
#include <QComboBox>
#include <QLineEdit>
#include <QTextEdit>
#include <QListWidget>
#include <QFrame>
#include <QElapsedTimer>
#include <QDebug>
#include <QTimer>
#include <QPainter>
#include <QtTest/QtTest>
#include <vector>
#include <algorithm>

/* ------------------------------------------------------------------
 * NOTE ON THE PRIMARY-ACTION-QUITS-IMMEDIATELY BEHAVIOR
 * ------------------------------------------------------------------
 * Set this to `true` to reproduce main.cpp's original wiring exactly
 * (Primary Action -> qApp->quit()), which means the script below will
 * only complete Step 1's first click before the app exits. Set to
 * `false` to skip that one connect() so the full 9-step script can run
 * end to end -- this is the recommended setting for an actual stress
 * test, since a test that quits after one click doesn't stress much.
 */
static constexpr bool REPRODUCE_ORIGINAL_QUIT_ON_PRIMARY_WIRING = false;

/* ------------------------------------------------------------------
 * PerfTestWindow -- identical to the provided main.cpp, except:
 *   - widget pointers are stored as members so the stress script can
 *     find them (original kept them as setupUI()-local variables)
 *   - the Primary-Action->quit() wiring is gated behind the constant
 *     above instead of being unconditional
 *   - row height / dropdown metrics are exposed via small getters
 * Everything else (stylesheet, geometry, no-MOC/no-Q_OBJECT
 * constraint, old-style connect syntax) is unchanged.
 * ------------------------------------------------------------------ */
class PerfTestWindow : public QWidget {
public:
    PerfTestWindow(QWidget *parent = nullptr) : QWidget(parent) {
        setWindowTitle("Qt - Scripted Stress Test (No MOC)");
        setFixedSize(800, 600);

        setStyleSheet(
            "QWidget { background-color: #1e1e1e; color: #e0e0e0; }"
            "QPushButton { background-color: #0d47a1; color: white; border: none; padding: 8px; border-radius: 4px; }"
            "QPushButton:hover { background-color: #1565c0; }"
            "QProgressBar { border: 1px solid #555; border-radius: 3px; text-align: center; }"
            "QProgressBar::chunk { background-color: #2196F3; border-radius: 3px; }"
            "QCheckBox { spacing: 8px; }"
            "QSlider::groove:horizontal { height: 8px; background: #424242; border-radius: 4px; }"
            "QSlider::handle:horizontal { background: #2196F3; width: 18px; margin: -5px 0; border-radius: 9px; }"
            "QComboBox { background-color: #424242; border: 1px solid #555; padding: 5px; border-radius: 3px; }"
            "QComboBox::drop-down { border: none; }"
            "QComboBox QAbstractItemView { background-color: #424242; selection-background-color: #1565c0; }"
            "QLineEdit { background-color: #424242; border: 1px solid #555; padding: 8px; border-radius: 3px; }"
            "QTextEdit { background-color: #424242; border: 1px solid #555; padding: 8px; border-radius: 3px; }"
            "QListWidget { background-color: #424242; border: 1px solid #555; border-radius: 3px; }"
            "QListWidget::item { padding: 8px; }"
            "QListWidget::item:selected { background-color: #1565c0; }"
        );

        setupUI();
        setupFPSCounter();

        qDebug() << "=== Qt Scripted Stress Test Started (No MOC) ===";
    }

    /* Accessors the stress script needs. All widgets below are the
     * exact same ones setupUI() creates in the original main.cpp;
     * only difference is they're members here instead of locals. */
    QPushButton *primaryBtn()  const { return m_btnPrimary; }
    QPushButton *secondaryBtn() const { return m_btnSecondary; }
    QPushButton *cancelBtn()   const { return m_btnCancel; }
    QCheckBox   *checkA()      const { return m_checkA; }
    QCheckBox   *checkB()      const { return m_checkB; }
    QCheckBox   *checkC()      const { return m_checkC; }
    QSlider     *volumeSlider() const { return m_volSlider; }
    QSlider     *brightnessSlider() const { return m_brightSlider; }
    QComboBox   *dropdown()    const { return m_dropdown; }
    QCheckBox   *switch1()     const { return m_switch1; }
    QCheckBox   *switch2()     const { return m_switch2; }
    QLineEdit   *nameInput()   const { return m_nameInput; }
    QLineEdit   *emailInput()  const { return m_emailInput; }
    QTextEdit   *messageInput() const { return m_messageInput; }
    QListWidget *listWidget()  const { return m_listWidget; }
    QPushButton *saveBtn()     const { return m_btnSave; }
    QPushButton *resetBtn()    const { return m_btnReset; }
    QPushButton *exportBtn()   const { return m_btnExport; }

    int frameCountSnapshot() const { return frameCount; }

protected:
    void paintEvent(QPaintEvent *event) override {
        QWidget::paintEvent(event);
        frameCount++;
    }

private:
    int frameCount = 0;
    QElapsedTimer timer;

    QPushButton *m_btnPrimary   = nullptr;
    QPushButton *m_btnSecondary = nullptr;
    QPushButton *m_btnCancel    = nullptr;
    QCheckBox   *m_checkA = nullptr, *m_checkB = nullptr, *m_checkC = nullptr;
    QSlider     *m_volSlider = nullptr, *m_brightSlider = nullptr;
    QComboBox   *m_dropdown = nullptr;
    QCheckBox   *m_switch1 = nullptr, *m_switch2 = nullptr;
    QLineEdit   *m_nameInput = nullptr, *m_emailInput = nullptr;
    QTextEdit   *m_messageInput = nullptr;
    QListWidget *m_listWidget = nullptr;
    QPushButton *m_btnSave = nullptr, *m_btnReset = nullptr, *m_btnExport = nullptr;

    void setupUI() {
        QLabel *titleLabel = new QLabel("Performance Test Dashboard", this);
        titleLabel->setGeometry(20, 15, 400, 25);
        titleLabel->setStyleSheet("font-size: 16px; font-weight: bold;");

        m_btnPrimary = new QPushButton("Primary Action", this);
        m_btnPrimary->setGeometry(20, 55, 140, 35);

        m_btnSecondary = new QPushButton("Secondary", this);
        m_btnSecondary->setGeometry(180, 55, 120, 35);

        m_btnCancel = new QPushButton("Cancel", this);
        m_btnCancel->setGeometry(320, 55, 100, 35);

        if (REPRODUCE_ORIGINAL_QUIT_ON_PRIMARY_WIRING) {
            QObject::connect(m_btnPrimary, SIGNAL(clicked()),
                            QApplication::instance(), SLOT(quit()));
        }

        QLabel *cpuLabel = new QLabel("CPU Usage", this);
        cpuLabel->setGeometry(20, 110, 200, 20);

        QProgressBar *cpuBar = new QProgressBar(this);
        cpuBar->setGeometry(20, 135, 200, 15);
        cpuBar->setValue(45);
        cpuBar->setTextVisible(false);

        QLabel *memLabel = new QLabel("Memory Usage", this);
        memLabel->setGeometry(240, 110, 200, 20);

        QProgressBar *memBar = new QProgressBar(this);
        memBar->setGeometry(240, 135, 200, 15);
        memBar->setValue(72);
        memBar->setTextVisible(false);

        m_checkA = new QCheckBox("Enable feature A", this);
        m_checkA->setGeometry(20, 170, 200, 25);

        m_checkB = new QCheckBox("Enable feature B", this);
        m_checkB->setGeometry(20, 200, 200, 25);

        m_checkC = new QCheckBox("Enable feature C", this);
        m_checkC->setGeometry(20, 230, 200, 25);

        QLabel *volLabel = new QLabel("Volume", this);
        volLabel->setGeometry(250, 170, 200, 20);

        m_volSlider = new QSlider(Qt::Horizontal, this);
        m_volSlider->setGeometry(250, 195, 200, 25);
        m_volSlider->setRange(0, 100);
        m_volSlider->setValue(75);

        QLabel *brightLabel = new QLabel("Brightness", this);
        brightLabel->setGeometry(250, 225, 200, 20);

        m_brightSlider = new QSlider(Qt::Horizontal, this);
        m_brightSlider->setGeometry(250, 250, 200, 25);
        m_brightSlider->setRange(0, 100);
        m_brightSlider->setValue(60);

        QLabel *optionLabel = new QLabel("Select Option", this);
        optionLabel->setGeometry(470, 110, 200, 20);

        m_dropdown = new QComboBox(this);
        m_dropdown->setGeometry(470, 135, 200, 30);
        m_dropdown->addItem("Option 1");
        m_dropdown->addItem("Option 2");
        m_dropdown->addItem("Option 3");
        m_dropdown->addItem("Option 4");
        m_dropdown->addItem("Option 5");

        QLabel *toggleLabel = new QLabel("Toggle Switches", this);
        toggleLabel->setGeometry(470, 180, 200, 20);

        m_switch1 = new QCheckBox(this);
        m_switch1->setGeometry(470, 205, 50, 25);
        m_switch1->setChecked(true);
        m_switch1->setText("");
        m_switch1->setStyleSheet(
            "QCheckBox::indicator { width: 50px; height: 25px; }"
            "QCheckBox::indicator:unchecked { background-color: #666; border-radius: 12px; }"
            "QCheckBox::indicator:checked { background-color: #4CAF50; border-radius: 12px; }"
        );

        QLabel *wifiLabel = new QLabel("WiFi", this);
        wifiLabel->setGeometry(530, 207, 100, 20);

        m_switch2 = new QCheckBox(this);
        m_switch2->setGeometry(470, 240, 50, 25);
        m_switch2->setText("");
        m_switch2->setStyleSheet(
            "QCheckBox::indicator { width: 50px; height: 25px; }"
            "QCheckBox::indicator:unchecked { background-color: #666; border-radius: 12px; }"
            "QCheckBox::indicator:checked { background-color: #4CAF50; border-radius: 12px; }"
        );

        QLabel *btLabel = new QLabel("Bluetooth", this);
        btLabel->setGeometry(530, 242, 100, 20);

        QFrame *divider = new QFrame(this);
        divider->setGeometry(20, 285, 760, 2);
        divider->setFrameShape(QFrame::HLine);
        divider->setStyleSheet("background-color: #555;");

        QLabel *textLabel = new QLabel("Text Input Fields", this);
        textLabel->setGeometry(20, 295, 400, 25);
        textLabel->setStyleSheet("font-size: 16px; font-weight: bold;");

        m_nameInput = new QLineEdit(this);
        m_nameInput->setGeometry(20, 330, 350, 35);
        m_nameInput->setPlaceholderText("Enter name...");

        m_emailInput = new QLineEdit(this);
        m_emailInput->setGeometry(20, 375, 350, 35);
        m_emailInput->setPlaceholderText("Enter email...");

        m_messageInput = new QTextEdit(this);
        m_messageInput->setGeometry(20, 420, 350, 80);
        m_messageInput->setPlaceholderText("Enter message...");

        QLabel *listLabel = new QLabel("Items List", this);
        listLabel->setGeometry(400, 295, 380, 25);
        listLabel->setStyleSheet("font-size: 16px; font-weight: bold;");

        m_listWidget = new QListWidget(this);
        m_listWidget->setGeometry(400, 330, 380, 170);
        m_listWidget->addItem("Item 1 - Dashboard");
        m_listWidget->addItem("Item 2 - Reports");
        m_listWidget->addItem("Item 3 - Analytics");
        m_listWidget->addItem("Item 4 - Settings");
        m_listWidget->addItem("Item 5 - Help");
        m_listWidget->addItem("Item 6 - About");

        m_btnSave = new QPushButton("Save", this);
        m_btnSave->setGeometry(20, 520, 100, 35);

        m_btnReset = new QPushButton("Reset", this);
        m_btnReset->setGeometry(140, 520, 100, 35);

        m_btnExport = new QPushButton("Export Data", this);
        m_btnExport->setGeometry(260, 520, 120, 35);
    }

    void setupFPSCounter() {
        frameCount = 0;
        timer.start();

        QTimer *fpsTimer = new QTimer(this);
        QObject::connect(fpsTimer, &QTimer::timeout, [this]() {
            double elapsed = timer.elapsed() / 1000.0;
            if (elapsed >= 1.0 && frameCount > 0) {
                double fps = frameCount / elapsed;
                qDebug() << "Qt FPS:" << fps
                         << "| Frame time:" << (elapsed * 1000.0) / frameCount << "ms"
                         << "| Total frames:" << frameCount;
                g_fpsSamples.push_back(fps);
                frameCount = 0;
                timer.restart();
            }
        });
        fpsTimer->start(1000);

        QTimer *repaintTimer = new QTimer(this);
        QObject::connect(repaintTimer, &QTimer::timeout, [this]() {
            this->update();
        });
        repaintTimer->start(16);
    }

public:
    /* Collected once-per-second FPS samples, for the final summary. */
    static std::vector<double> g_fpsSamples;
};

std::vector<double> PerfTestWindow::g_fpsSamples;

/* ------------------------------------------------------------------
 * Scripted interaction sequence
 * ------------------------------------------------------------------
 * Each step uses real QTest synthetic input:
 *   - QTest::mouseClick(widget, Qt::LeftButton, ..., localPos)
 *   - QTest::mouseMove / mousePress / mouseRelease for drags, since
 *     QTest doesn't have a single "drag" helper -- a slider drag is a
 *     press at the handle, N move events, then a release, same as a
 *     real user would generate.
 *   - setText() / setPlainText() to populate QLineEdit/QTextEdit directly,
 *     rather than QTest::keyClicks() -- see Step 7 for why
 *   - QTest::qWait(ms) to let the event loop process paint/timer events
 *     between steps, matching how a real user's actions are spaced out
 *     rather than instantaneous.
 * ------------------------------------------------------------------ */
static void runStressScript(PerfTestWindow *win) {
    using namespace std;

    qDebug() << "\n--- Step 1: Clicking top buttons ---";
    QTest::mouseClick(win->secondaryBtn(), Qt::LeftButton);
    QTest::qWait(50);
    QTest::mouseClick(win->cancelBtn(), Qt::LeftButton);
    QTest::qWait(50);
    /* Primary Action clicked last/optionally: if
     * REPRODUCE_ORIGINAL_QUIT_ON_PRIMARY_WIRING is true, this is the
     * point the app will quit, same as the original main.cpp's wiring
     * would do for a real user clicking it. */
    QTest::mouseClick(win->primaryBtn(), Qt::LeftButton);
    QTest::qWait(50);

    qDebug() << "\n--- Step 2: Toggling checkboxes A/B/C ---";
    QTest::mouseClick(win->checkA(), Qt::LeftButton);
    QTest::qWait(30);
    QTest::mouseClick(win->checkB(), Qt::LeftButton);
    QTest::qWait(30);
    QTest::mouseClick(win->checkC(), Qt::LeftButton);
    QTest::qWait(30);

    qDebug() << "\n--- Step 3: Dragging Volume slider left -> right ---";
    {
        QSlider *s = win->volumeSlider();
        QRect r = s->geometry();
        QPoint startLocal(5, r.height() / 2);
        QPoint endLocal(r.width() - 5, r.height() / 2);
        QTest::mousePress(s, Qt::LeftButton, Qt::NoModifier, startLocal);
        const int STEPS = 10;
        for (int i = 1; i <= STEPS; i++) {
            double t = double(i) / STEPS;
            QPoint p(startLocal.x() + int((endLocal.x() - startLocal.x()) * t), startLocal.y());
            QTest::mouseMove(s, p);
            QTest::qWait(15);
        }
        QTest::mouseRelease(s, Qt::LeftButton, Qt::NoModifier, endLocal);
        QTest::qWait(30);
    }

    qDebug() << "\n--- Step 4: Dragging Brightness slider right -> left ---";
    {
        QSlider *s = win->brightnessSlider();
        QRect r = s->geometry();
        QPoint startLocal(r.width() - 5, r.height() / 2);
        QPoint endLocal(5, r.height() / 2);
        QTest::mousePress(s, Qt::LeftButton, Qt::NoModifier, startLocal);
        const int STEPS = 10;
        for (int i = 1; i <= STEPS; i++) {
            double t = double(i) / STEPS;
            QPoint p(startLocal.x() + int((endLocal.x() - startLocal.x()) * t), startLocal.y());
            QTest::mouseMove(s, p);
            QTest::qWait(15);
        }
        QTest::mouseRelease(s, Qt::LeftButton, Qt::NoModifier, endLocal);
        QTest::qWait(30);
    }

    qDebug() << "\n--- Step 5: Opening dropdown and selecting each option ---";
    {
        QComboBox *dd = win->dropdown();
        for (int i = 0; i < dd->count(); i++) {
            /* setCurrentIndex + emit is what a real popup-list click
             * ultimately produces; QTest has no portable helper for
             * clicking inside a QComboBox's native popup view across
             * platforms, so we drive it the same way Qt's own
             * autotests do: open, then set index directly. */
            QTest::mouseClick(dd, Qt::LeftButton);
            QTest::qWait(50);
            dd->setCurrentIndex(i);
            QTest::qWait(30);
        }
    }

    qDebug() << "\n--- Step 6: Flipping WiFi and Bluetooth switches ---";
    QTest::mouseClick(win->switch1(), Qt::LeftButton);
    QTest::qWait(30);
    QTest::mouseClick(win->switch2(), Qt::LeftButton);
    QTest::qWait(30);

    qDebug() << "\n--- Step 7: Setting name/email/message field text ---";
    /* Click to focus each field (same as before), then set its text
     * directly via the widget API instead of synthesizing a key event
     * per character. Kept for parity with the Aroma comparison target,
     * whose text-field step only clicks to focus and never injects
     * keystrokes -- QTest::keyClicks() here was doing strictly more
     * simulated work per field than Aroma's side, which skewed the
     * frame-time numbers for this step. */
    QTest::mouseClick(win->nameInput(), Qt::LeftButton);
    win->nameInput()->setText("Stress Test User");
    QTest::qWait(30);
    QTest::mouseClick(win->emailInput(), Qt::LeftButton);
    win->emailInput()->setText("stress@test.local");
    QTest::qWait(30);
    QTest::mouseClick(win->messageInput()->viewport(), Qt::LeftButton);
    win->messageInput()->setPlainText("Scripted stress test message.");
    QTest::qWait(30);

    qDebug() << "\n--- Step 8: Clicking every row in the list widget ---";
    {
        QListWidget *lw = win->listWidget();
        for (int i = 0; i < lw->count(); i++) {
            QRect itemRect = lw->visualItemRect(lw->item(i));
            QTest::mouseClick(lw->viewport(), Qt::LeftButton, Qt::NoModifier, itemRect.center());
            QTest::qWait(30);
        }
    }

    qDebug() << "\n--- Step 9: Clicking Save / Reset / Export ---";
    QTest::mouseClick(win->saveBtn(), Qt::LeftButton);
    QTest::qWait(30);
    QTest::mouseClick(win->resetBtn(), Qt::LeftButton);
    QTest::qWait(30);
    QTest::mouseClick(win->exportBtn(), Qt::LeftButton);
    QTest::qWait(30);

    qDebug() << "\n=== Qt Scripted Stress Test Complete ===";

    /* Summarize collected per-second FPS samples, then quit. */
    const auto &samples = PerfTestWindow::g_fpsSamples;
    if (!samples.empty()) {
        double total = 0.0, mn = samples[0], mx = samples[0];
        for (double v : samples) {
            total += v;
            mn = std::min(mn, v);
            mx = std::max(mx, v);
        }
        qDebug() << "FPS samples collected:" << (int)samples.size();
        qDebug() << "Average FPS:" << (total / samples.size());
        qDebug() << "Min FPS:" << mn;
        qDebug() << "Max FPS:" << mx;
    } else {
        qDebug() << "No full 1-second FPS window elapsed during the script; "
                     "run took less than 1s or REPRODUCE_ORIGINAL_QUIT_ON_PRIMARY_WIRING "
                     "ended it early.";
    }

    QApplication::instance()->quit();
}

int main(int argc, char *argv[]) {
    QApplication app(argc, argv);

    PerfTestWindow window;
    window.show();

    /* Defer the script by one event-loop turn so the window is fully
     * shown/laid out (and gets its first paintEvent) before synthetic
     * input starts landing on it. */
    QTimer::singleShot(200, [&window]() {
        runStressScript(&window);
    });

    return app.exec();
}