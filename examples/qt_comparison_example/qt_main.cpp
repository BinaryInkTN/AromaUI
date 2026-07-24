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

static constexpr bool REPRODUCE_ORIGINAL_QUIT_ON_PRIMARY_WIRING = false;

// ---------------------------------------------------------------------------
// Shared benchmark contract (must match the Aroma harness exactly):
//   - fixed total wall-clock script duration, split evenly across the 9 steps
//   - fixed settle delay before the script starts
//   - repaint tick and FPS sampling run on their own independent cadence
//   - a trailing partial FPS window is reported, never silently dropped
// ---------------------------------------------------------------------------
static constexpr int SETTLE_DELAY_MS = 200;
static constexpr int STEP_COUNT = 9;
static constexpr double TOTAL_SCRIPT_DURATION_MS = 6000.0;
static constexpr double PER_STEP_BUDGET_MS = TOTAL_SCRIPT_DURATION_MS / STEP_COUNT;
static constexpr int REPAINT_TICK_MS = 16;
static constexpr int FPS_WINDOW_MS = 1000;

class PerfTestWindow : public QWidget
{
public:
    PerfTestWindow(QWidget *parent = nullptr) : QWidget(parent)
    {
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
            "QListWidget::item:selected { background-color: #1565c0; }");

        setupUI();
        setupFPSCounter();

        qDebug() << "=== Qt Scripted Stress Test Started (No MOC) ===";
    }

    QPushButton *primaryBtn() const { return m_btnPrimary; }
    QPushButton *secondaryBtn() const { return m_btnSecondary; }
    QPushButton *cancelBtn() const { return m_btnCancel; }
    QCheckBox *checkA() const { return m_checkA; }
    QCheckBox *checkB() const { return m_checkB; }
    QCheckBox *checkC() const { return m_checkC; }
    QSlider *volumeSlider() const { return m_volSlider; }
    QSlider *brightnessSlider() const { return m_brightSlider; }
    QComboBox *dropdown() const { return m_dropdown; }
    QCheckBox *switch1() const { return m_switch1; }
    QCheckBox *switch2() const { return m_switch2; }
    QLineEdit *nameInput() const { return m_nameInput; }
    QLineEdit *emailInput() const { return m_emailInput; }
    QTextEdit *messageInput() const { return m_messageInput; }
    QListWidget *listWidget() const { return m_listWidget; }
    QPushButton *saveBtn() const { return m_btnSave; }
    QPushButton *resetBtn() const { return m_btnReset; }
    QPushButton *exportBtn() const { return m_btnExport; }

    int frameCountSnapshot() const { return frameCount; }

    // Mark the window dirty only when a driven action actually happened,
    // instead of forcing a repaint every loop turn regardless of state.
    void markDirtyFromAction() { update(); }

protected:
    void paintEvent(QPaintEvent *event) override
    {
        QWidget::paintEvent(event);
        frameCount++;
    }

private:
    int frameCount = 0;
    QElapsedTimer timer;

    QPushButton *m_btnPrimary = nullptr;
    QPushButton *m_btnSecondary = nullptr;
    QPushButton *m_btnCancel = nullptr;
    QCheckBox *m_checkA = nullptr, *m_checkB = nullptr, *m_checkC = nullptr;
    QSlider *m_volSlider = nullptr, *m_brightSlider = nullptr;
    QComboBox *m_dropdown = nullptr;
    QCheckBox *m_switch1 = nullptr, *m_switch2 = nullptr;
    QLineEdit *m_nameInput = nullptr, *m_emailInput = nullptr;
    QTextEdit *m_messageInput = nullptr;
    QListWidget *m_listWidget = nullptr;
    QPushButton *m_btnSave = nullptr, *m_btnReset = nullptr, *m_btnExport = nullptr;

    void setupUI()
    {
        QLabel *titleLabel = new QLabel("Performance Test Dashboard", this);
        titleLabel->setGeometry(20, 15, 400, 25);
        titleLabel->setStyleSheet("font-size: 16px; font-weight: bold;");

        m_btnPrimary = new QPushButton("Primary Action", this);
        m_btnPrimary->setGeometry(20, 55, 140, 35);

        m_btnSecondary = new QPushButton("Secondary", this);
        m_btnSecondary->setGeometry(180, 55, 120, 35);

        m_btnCancel = new QPushButton("Cancel", this);
        m_btnCancel->setGeometry(320, 55, 100, 35);

        if (REPRODUCE_ORIGINAL_QUIT_ON_PRIMARY_WIRING)
        {
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
            "QCheckBox::indicator:checked { background-color: #4CAF50; border-radius: 12px; }");

        QLabel *wifiLabel = new QLabel("WiFi", this);
        wifiLabel->setGeometry(530, 207, 100, 20);

        m_switch2 = new QCheckBox(this);
        m_switch2->setGeometry(470, 240, 50, 25);
        m_switch2->setText("");
        m_switch2->setStyleSheet(
            "QCheckBox::indicator { width: 50px; height: 25px; }"
            "QCheckBox::indicator:unchecked { background-color: #666; border-radius: 12px; }"
            "QCheckBox::indicator:checked { background-color: #4CAF50; border-radius: 12px; }");

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

    void setupFPSCounter()
    {
        frameCount = 0;
        timer.start();

        // Repaint tick is independent of the test-driving cadence below, so FPS
        // reflects render cost rather than however fast the script happens to run.
        QTimer *repaintTimer = new QTimer(this);
        QObject::connect(repaintTimer, &QTimer::timeout, [this]()
                         { this->update(); });
        repaintTimer->start(REPAINT_TICK_MS);
    }

public:
    static std::vector<double> g_fpsSamples;
    static QElapsedTimer g_fpsWindowTimer;
    static int g_fpsWindowFrameCount;

    // Called once per paint to also drive windowed FPS sampling independent of
    // any single-second boundary check, so a trailing partial window is still
    // captured instead of silently discarded.
};

std::vector<double> PerfTestWindow::g_fpsSamples;

static void sampleFpsWindow(PerfTestWindow *win, QElapsedTimer &windowTimer, int &windowFrames, bool forceFlush)
{
    // Kept separate from paintEvent so both a full 1s window and a final
    // partial window at shutdown are recorded the same way.
    double elapsedMs = windowTimer.elapsed();
    if (elapsedMs >= FPS_WINDOW_MS || (forceFlush && windowFrames > 0 && elapsedMs > 0))
    {
        double elapsedS = elapsedMs / 1000.0;
        double fps = windowFrames / elapsedS;
        qDebug() << "Qt FPS:" << fps
                 << "| Frame time:" << elapsedMs / windowFrames << "ms"
                 << "| Total frames:" << windowFrames
                 << (forceFlush ? "(final partial window)" : "");
        PerfTestWindow::g_fpsSamples.push_back(fps);
        windowFrames = 0;
        windowTimer.restart();
    }
}

static void runStressScript(PerfTestWindow *win)
{
    using namespace std;

    // Independent bookkeeping for windowed FPS sampling; runs alongside the
    // fixed-duration step budget below rather than being tied to step count.
    QElapsedTimer fpsWindowTimer;
    int fpsWindowFrames = 0;
    fpsWindowTimer.start();

    auto waitAndSample = [&](int ms)
    {
        // qWait already pumps the event loop (and therefore paintEvent, which
        // increments frameCount); we piggyback windowed sampling on top of it
        // so both harnesses use the same "did a paint happen" bookkeeping.
        int elapsedSoFar = 0;
        const int slice = 16;
        while (elapsedSoFar < ms)
        {
            int thisSlice = std::min(slice, ms - elapsedSoFar);
            QTest::qWait(thisSlice);
            elapsedSoFar += thisSlice;
            sampleFpsWindow(win, fpsWindowTimer, fpsWindowFrames, false);
        }
    };

    // Each step gets an equal, fixed share of the total script duration
    // (PER_STEP_BUDGET_MS), matching the Aroma harness's per-step budget.
    // Sub-actions within a step split that budget evenly among themselves.

    qDebug() << "\n--- Step 1: Clicking top buttons ---";
    {
        const double sub = PER_STEP_BUDGET_MS / 3.0;
        QTest::mouseClick(win->secondaryBtn(), Qt::LeftButton);
        win->markDirtyFromAction();
        waitAndSample((int)sub);
        QTest::mouseClick(win->cancelBtn(), Qt::LeftButton);
        win->markDirtyFromAction();
        waitAndSample((int)sub);
        QTest::mouseClick(win->primaryBtn(), Qt::LeftButton);
        win->markDirtyFromAction();
        waitAndSample((int)sub);
    }

    qDebug() << "\n--- Step 2: Toggling checkboxes A/B/C ---";
    {
        const double sub = PER_STEP_BUDGET_MS / 3.0;
        QTest::mouseClick(win->checkA(), Qt::LeftButton);
        win->markDirtyFromAction();
        waitAndSample((int)sub);
        QTest::mouseClick(win->checkB(), Qt::LeftButton);
        win->markDirtyFromAction();
        waitAndSample((int)sub);
        QTest::mouseClick(win->checkC(), Qt::LeftButton);
        win->markDirtyFromAction();
        waitAndSample((int)sub);
    }

    qDebug() << "\n--- Step 3: Dragging Volume slider left -> right ---";
    {
        QSlider *s = win->volumeSlider();
        QRect r = s->geometry();
        QPoint startLocal(5, r.height() / 2);
        QPoint endLocal(r.width() - 5, r.height() / 2);
        QTest::mousePress(s, Qt::LeftButton, Qt::NoModifier, startLocal);
        win->markDirtyFromAction();
        const int STEPS = 10;
        const double perMoveMs = (PER_STEP_BUDGET_MS * 0.85) / STEPS;
        for (int i = 1; i <= STEPS; i++)
        {
            double t = double(i) / STEPS;
            QPoint p(startLocal.x() + int((endLocal.x() - startLocal.x()) * t), startLocal.y());
            QTest::mouseMove(s, p);
            win->markDirtyFromAction();
            waitAndSample((int)perMoveMs);
        }
        QTest::mouseRelease(s, Qt::LeftButton, Qt::NoModifier, endLocal);
        win->markDirtyFromAction();
        waitAndSample((int)(PER_STEP_BUDGET_MS * 0.15));
    }

    qDebug() << "\n--- Step 4: Dragging Brightness slider right -> left ---";
    {
        QSlider *s = win->brightnessSlider();
        QRect r = s->geometry();
        QPoint startLocal(r.width() - 5, r.height() / 2);
        QPoint endLocal(5, r.height() / 2);
        QTest::mousePress(s, Qt::LeftButton, Qt::NoModifier, startLocal);
        win->markDirtyFromAction();
        const int STEPS = 10;
        const double perMoveMs = (PER_STEP_BUDGET_MS * 0.85) / STEPS;
        for (int i = 1; i <= STEPS; i++)
        {
            double t = double(i) / STEPS;
            QPoint p(startLocal.x() + int((endLocal.x() - startLocal.x()) * t), startLocal.y());
            QTest::mouseMove(s, p);
            win->markDirtyFromAction();
            waitAndSample((int)perMoveMs);
        }
        QTest::mouseRelease(s, Qt::LeftButton, Qt::NoModifier, endLocal);
        win->markDirtyFromAction();
        waitAndSample((int)(PER_STEP_BUDGET_MS * 0.15));
    }

    qDebug() << "\n--- Step 5: Opening dropdown and selecting each option ---";
    {
        QComboBox *dd = win->dropdown();
        const double sub = PER_STEP_BUDGET_MS / dd->count();
        for (int i = 0; i < dd->count(); i++)
        {
            QTest::mouseClick(dd, Qt::LeftButton);
            win->markDirtyFromAction();
            waitAndSample((int)(sub * 0.6));
            dd->setCurrentIndex(i);
            win->markDirtyFromAction();
            waitAndSample((int)(sub * 0.4));
        }
    }

    qDebug() << "\n--- Step 6: Flipping WiFi and Bluetooth switches ---";
    {
        const double sub = PER_STEP_BUDGET_MS / 2.0;
        QTest::mouseClick(win->switch1(), Qt::LeftButton);
        win->markDirtyFromAction();
        waitAndSample((int)sub);
        QTest::mouseClick(win->switch2(), Qt::LeftButton);
        win->markDirtyFromAction();
        waitAndSample((int)sub);
    }

    qDebug() << "\n--- Step 7: Setting name/email/message field text ---";
    {
        const double sub = PER_STEP_BUDGET_MS / 3.0;
        QTest::mouseClick(win->nameInput(), Qt::LeftButton);
        win->nameInput()->setText("Stress Test User");
        win->markDirtyFromAction();
        waitAndSample((int)sub);
        QTest::mouseClick(win->emailInput(), Qt::LeftButton);
        win->emailInput()->setText("stress@test.local");
        win->markDirtyFromAction();
        waitAndSample((int)sub);
        QTest::mouseClick(win->messageInput()->viewport(), Qt::LeftButton);
        win->messageInput()->setPlainText("Scripted stress test message.");
        win->markDirtyFromAction();
        waitAndSample((int)sub);
    }

    qDebug() << "\n--- Step 8: Clicking every row in the list widget ---";
    {
        QListWidget *lw = win->listWidget();
        const double sub = PER_STEP_BUDGET_MS / lw->count();
        for (int i = 0; i < lw->count(); i++)
        {
            QRect itemRect = lw->visualItemRect(lw->item(i));
            QTest::mouseClick(lw->viewport(), Qt::LeftButton, Qt::NoModifier, itemRect.center());
            win->markDirtyFromAction();
            waitAndSample((int)sub);
        }
    }

    qDebug() << "\n--- Step 9: Clicking Save / Reset / Export ---";
    {
        const double sub = PER_STEP_BUDGET_MS / 3.0;
        QTest::mouseClick(win->saveBtn(), Qt::LeftButton);
        win->markDirtyFromAction();
        waitAndSample((int)sub);
        QTest::mouseClick(win->resetBtn(), Qt::LeftButton);
        win->markDirtyFromAction();
        waitAndSample((int)sub);
        QTest::mouseClick(win->exportBtn(), Qt::LeftButton);
        win->markDirtyFromAction();
        waitAndSample((int)sub);
    }

    qDebug() << "\n=== Qt Scripted Stress Test Complete ===";

    // Flush whatever's left in the current window instead of discarding it,
    // so a run that doesn't land exactly on a window boundary still counts.
    sampleFpsWindow(win, fpsWindowTimer, fpsWindowFrames, true);

    const auto &samples = PerfTestWindow::g_fpsSamples;
    if (!samples.empty())
    {
        double total = 0.0, mn = samples[0], mx = samples[0];
        for (double v : samples)
        {
            total += v;
            mn = std::min(mn, v);
            mx = std::max(mx, v);
        }
        qDebug() << "FPS samples collected:" << (int)samples.size();
        qDebug() << "Average FPS:" << (total / samples.size());
        qDebug() << "Min FPS:" << mn;
        qDebug() << "Max FPS:" << mx;
    }
    else
    {
        qDebug() << "No FPS window elapsed during the script.";
    }

    QApplication::instance()->quit();
}

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);

    PerfTestWindow window;
    window.show();

    // Fixed settle delay before the script starts, matching the Aroma harness.
    QTimer::singleShot(SETTLE_DELAY_MS, [&window]()
                       { runStressScript(&window); });

    return app.exec();
}