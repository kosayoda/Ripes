#include "mainwindow.h"
#include "ui_mainwindow.h"

#include "cachetab.h"
#include "defines.h"
#include "edittab.h"
#include "iotab.h"
#include "loaddialog.h"
#include "memorytab.h"
#include "processorhandler.h"
#include "processortab.h"
#include "registerwidget.h"
#include "ripessettings.h"
#include "savedialog.h"
#include "settingsdialog.h"
#include "syscall/syscallviewer.h"
#include "syscall/systemio.h"
#include "version/version.h"

#include "fancytabbar/fancytabbar.h"

#include <QCloseEvent>
#include <QDesktopServices>
#include <QFileDialog>
#include <QFontDatabase>
#include <QIcon>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QStackedWidget>
#include <QStatusBar>
#include <QTemporaryFile>
#include <QTextStream>

namespace Ripes {

MainWindow::MainWindow(QWidget* parent) : QMainWindow(parent), m_ui(new Ui::MainWindow) {
    m_ui->setupUi(this);
    setWindowTitle("Ripes");
    setWindowIcon(QIcon(":/icons/logo.svg"));
    m_ui->actionOpen_wiki->setIcon(QIcon(":/icons/info.svg"));

    // Initialize processor handler
    ProcessorHandler::get();

    // Initialize fonts
    QFontDatabase::addApplicationFont(":/fonts/Inconsolata/Inconsolata-Regular.ttf");
    QFontDatabase::addApplicationFont(":/fonts/Inconsolata/Inconsolata-Bold.ttf");

    // Create tabs
    m_stackedTabs = new QStackedWidget(this);
    m_ui->centrallayout->addWidget(m_stackedTabs);

    auto* controlToolbar = addToolBar("Simulator control");
    controlToolbar->setVisible(true);  // Always visible

    auto* editToolbar = addToolBar("Edit");
    editToolbar->setVisible(false);
    auto* editTab = new EditTab(editToolbar, this);
    m_stackedTabs->insertWidget(EditTabID, editTab);
    m_tabWidgets[EditTabID] = {editTab, editToolbar};

    auto* processorToolbar = addToolBar("Processor");
    processorToolbar->setVisible(false);
    auto* processorTab = new ProcessorTab(controlToolbar, processorToolbar, this);
    m_stackedTabs->insertWidget(ProcessorTabID, processorTab);
    m_tabWidgets[ProcessorTabID] = {processorTab, processorToolbar};

    auto* cacheToolbar = addToolBar("Cache");
    cacheToolbar->setVisible(false);
    auto* cacheTab = new CacheTab(cacheToolbar, this);
    m_stackedTabs->insertWidget(CacheTabID, cacheTab);
    m_tabWidgets[CacheTabID] = {cacheTab, cacheToolbar};

    auto* memoryToolbar = addToolBar("Memory");
    memoryToolbar->setVisible(false);
    auto* memoryTab = new MemoryTab(memoryToolbar, this);
    m_stackedTabs->insertWidget(MemoryTabID, memoryTab);
    m_tabWidgets[MemoryTabID] = {memoryTab, memoryToolbar};

    auto* IOToolbar = addToolBar("I/O");
    IOToolbar->setVisible(false);
    auto* IOTab = new class IOTab(IOToolbar, this);
    m_stackedTabs->insertWidget(IOTabID, IOTab);
    m_tabWidgets[IOTabID] = {IOTab, IOToolbar};

    // Setup tab bar
    m_ui->tabbar->addFancyTab(QIcon(":/icons/binary-code.svg"), "Editor");
    m_ui->tabbar->addFancyTab(QIcon(":/icons/cpu.svg"), "Processor");
    m_ui->tabbar->addFancyTab(QIcon(":/icons/server.svg"), "Cache");
    m_ui->tabbar->addFancyTab(QIcon(":/icons/ram-memory.svg"), "Memory");
    m_ui->tabbar->addFancyTab(QIcon(":/icons/led.svg"), "I/O");
    connect(m_ui->tabbar, &FancyTabBar::activeIndexChanged, this, &MainWindow::tabChanged);
    connect(m_ui->tabbar, &FancyTabBar::activeIndexChanged, m_stackedTabs, &QStackedWidget::setCurrentIndex);
    connect(m_ui->tabbar, &FancyTabBar::activeIndexChanged, editTab, &EditTab::updateProgramViewerHighlighting);

    setupMenus();

    // setup and connect widgets
    connect(editTab, &EditTab::programChanged, ProcessorHandler::get(), &ProcessorHandler::loadProgram);
    connect(editTab, &EditTab::editorStateChanged, [=] { this->m_hasSavedFile = false; });

    connect(ProcessorHandler::get(), &ProcessorHandler::exit, processorTab, &ProcessorTab::processorFinished);
    connect(ProcessorHandler::get(), &ProcessorHandler::runFinished, processorTab, &ProcessorTab::runFinished);

    connect(&SystemIO::get(), &SystemIO::doPrint, processorTab, &ProcessorTab::printToLog);

    // Setup status bar
    setupStatusBar();

    // Reset and program reload signals
    connect(ProcessorHandler::get(), &ProcessorHandler::processorChanged, editTab, &EditTab::onProcessorChanged);
    connect(ProcessorHandler::get(), &ProcessorHandler::stopping, processorTab, &ProcessorTab::pause);

    connect(ProcessorHandler::get(), &ProcessorHandler::processorReset, [=] { SystemIO::reset(); });

    connect(m_ui->actionSystem_calls, &QAction::triggered, [=] {
        SyscallViewer v;
        v.exec();
    });
    connect(m_ui->actionOpen_wiki, &QAction::triggered, this, &MainWindow::wiki);
    connect(m_ui->actionVersion, &QAction::triggered, this, &MainWindow::version);
    connect(m_ui->actionSettings, &QAction::triggered, this, &MainWindow::settingsTriggered);

    connect(cacheTab, &CacheTab::focusAddressChanged, memoryTab, &MemoryTab::setCentralAddress);

    m_currentTabID = ProcessorTabID;
    m_ui->tabbar->setActiveIndex(m_currentTabID);
}

#define setupStatusWidget(name)                                                                                       \
    auto* name##StatusLabel = new QLabel(this);                                                                       \
    statusBar()->addWidget(name##StatusLabel);                                                                        \
    connect(&name##StatusManager::get().emitter, &StatusEmitter::statusChanged, name##StatusLabel, &QLabel::setText); \
    connect(&name##StatusManager::get().emitter, &StatusEmitter::clear, name##StatusLabel, &QLabel::clear);

void MainWindow::setupStatusBar() {
    statusBar()->showMessage("");

    // Setup processorhandler status widget
    setupStatusWidget(Processor);

    // Setup syscall status widget
    setupStatusWidget(Syscall);

    // Setup systemIO status widget
    setupStatusWidget(SystemIO);
}

void MainWindow::tabChanged(int index) {
    m_tabWidgets.at(m_currentTabID).toolbar->setVisible(false);
    m_tabWidgets.at(m_currentTabID).tab->tabVisibilityChanged(false);
    m_currentTabID = static_cast<TabIndex>(index);
    m_tabWidgets.at(m_currentTabID).toolbar->setVisible(true);
    m_tabWidgets.at(m_currentTabID).tab->tabVisibilityChanged(true);
}

void MainWindow::fitToView() {
    static_cast<ProcessorTab*>(m_tabWidgets.at(ProcessorTabID).tab)->fitToView();
}

void MainWindow::setupMenus() {
    // Edit actions
    const QIcon newIcon = QIcon(":/icons/file.svg");
    auto* newAction = new QAction(newIcon, "New Program", this);
    newAction->setShortcut(QKeySequence::New);
    connect(newAction, &QAction::triggered, this, &MainWindow::newProgramTriggered);
    m_ui->menuFile->addAction(newAction);

    const QIcon loadIcon = QIcon(":/icons/loadfile.svg");
    auto* loadAction = new QAction(loadIcon, "Load Program", this);
    loadAction->setShortcut(QKeySequence::Open);
    connect(loadAction, &QAction::triggered, [=] { this->loadFileTriggered(); });
    m_ui->menuFile->addAction(loadAction);

    m_ui->menuFile->addSeparator();

    auto* examplesMenu = m_ui->menuFile->addMenu("Load Example...");
    setupExamplesMenu(examplesMenu);

    m_ui->menuFile->addSeparator();

    const QIcon saveIcon = QIcon(":/icons/save.svg");
    auto* saveAction = new QAction(saveIcon, "Save File", this);
    saveAction->setShortcut(QKeySequence::Save);
    connect(saveAction, &QAction::triggered, this, &MainWindow::saveFilesTriggered);
    connect(static_cast<EditTab*>(m_tabWidgets.at(EditTabID).tab), &EditTab::editorStateChanged,
            [saveAction](bool enabled) { saveAction->setEnabled(enabled); });
    m_ui->menuFile->addAction(saveAction);

    const QIcon saveAsIcon = QIcon(":/icons/saveas.svg");
    auto* saveAsAction = new QAction(saveAsIcon, "Save File As...", this);
    saveAsAction->setShortcut(QKeySequence::SaveAs);
    connect(saveAsAction, &QAction::triggered, this, &MainWindow::saveFilesAsTriggered);
    connect(static_cast<EditTab*>(m_tabWidgets.at(EditTabID).tab), &EditTab::editorStateChanged,
            [saveAsAction](bool enabled) { saveAsAction->setEnabled(enabled); });
    m_ui->menuFile->addAction(saveAsAction);

    m_ui->menuFile->addSeparator();

    const QIcon exitIcon = QIcon(":/icons/cancel.svg");
    auto* exitAction = new QAction(exitIcon, "Exit", this);
    exitAction->setShortcut(QKeySequence::Quit);
    connect(exitAction, &QAction::triggered, this, &MainWindow::close);
    m_ui->menuFile->addAction(exitAction);

    m_ui->menuView->addAction(static_cast<ProcessorTab*>(m_tabWidgets.at(ProcessorTabID).tab)->m_darkmodeAction);
    m_ui->menuView->addAction(static_cast<ProcessorTab*>(m_tabWidgets.at(ProcessorTabID).tab)->m_displayValuesAction);
}

MainWindow::~MainWindow() {
    delete m_ui;
}

void MainWindow::setupExamplesMenu(QMenu* parent) {
    const auto assemblyExamples = QDir(":/examples/assembly/").entryList(QDir::Files);
    auto* assemblyMenu = parent->addMenu("Assembly");
    if (!assemblyExamples.isEmpty()) {
        for (const auto& fileName : assemblyExamples) {
            assemblyMenu->addAction(fileName, this, [=] {
                LoadFileParams parms;
                parms.filepath = QString(":/examples/assembly/") + fileName;
                parms.type = SourceType::Assembly;
                static_cast<EditTab*>(m_tabWidgets.at(EditTabID).tab)->loadExternalFile(parms);
                m_hasSavedFile = false;
            });
        }
    }

    const auto cExamples = QDir(":/examples/C/").entryList(QDir::Files);
    auto* cMenu = parent->addMenu("C");
    if (!cExamples.isEmpty()) {
        for (const auto& fileName : cExamples) {
            cMenu->addAction(fileName, this, [=] {
                LoadFileParams parms;
                parms.filepath = QString(":/examples/C/") + fileName;
                parms.type = SourceType::C;
                static_cast<EditTab*>(m_tabWidgets.at(EditTabID).tab)->loadExternalFile(parms);
                m_hasSavedFile = false;
            });
        }
    }

    const auto ELFExamples = QDir(":/examples/ELF/").entryList(QDir::Files);
    auto* elfMenu = parent->addMenu("ELF (precompiled C)");
    if (!ELFExamples.isEmpty()) {
        for (const auto& fileName : ELFExamples) {
            elfMenu->addAction(fileName, this, [=] {
                // ELFIO Cannot read directly from the bundled resource file, so copy the ELF file to a temporary file
                // before loading the program.
                QTemporaryFile* tmpELFFile = QTemporaryFile::createNativeFile(":/examples/ELF/" + fileName);
                if (!tmpELFFile->open()) {
                    QMessageBox::warning(this, "Error", "Could not create temporary ELF file");
                    return;
                }

                LoadFileParams parms;
                parms.filepath = tmpELFFile->fileName();
                parms.type = SourceType::ExternalELF;
                static_cast<EditTab*>(m_tabWidgets.at(EditTabID).tab)->loadExternalFile(parms);
                m_hasSavedFile = false;
                tmpELFFile->remove();
            });
        }
    }
}

void MainWindow::closeEvent(QCloseEvent* event) {
    if (static_cast<EditTab*>(m_tabWidgets.at(EditTabID).tab)->isEditorEnabled() &&
        !static_cast<EditTab*>(m_tabWidgets.at(EditTabID).tab)->getAssemblyText().isEmpty()) {
        QMessageBox saveMsgBox(this);
        saveMsgBox.setWindowTitle("Ripes");
        saveMsgBox.setText("Save current program before exiting?");
        saveMsgBox.setStandardButtons(QMessageBox::Yes | QMessageBox::No | QMessageBox::Cancel);

        const auto result = saveMsgBox.exec();
        if (result == QMessageBox::Cancel) {
            // Dont exit
            event->ignore();
            return;
        } else if (result == QMessageBox::Yes) {
            saveFilesTriggered();
        }
    }

    // Emit an observable signal to indicate that the application is about to close
    RipesSettings::setValue(RIPES_GLOBALSIGNAL_QUIT, 0);
    QMainWindow::closeEvent(event);
}

void MainWindow::loadFileTriggered() {
    static_cast<ProcessorTab*>(m_tabWidgets.at(ProcessorTabID).tab)->pause();
    LoadDialog diag;
    if (!diag.exec())
        return;

    static_cast<EditTab*>(m_tabWidgets.at(EditTabID).tab)->loadExternalFile(diag.getParams());
    m_hasSavedFile = false;
}

void MainWindow::wiki() {
    QDesktopServices::openUrl(QUrl(QString("https://github.com/mortbopet/Ripes/wiki")));
}

void MainWindow::version() {
    QMessageBox aboutDialog(this);
    aboutDialog.setText("Ripes version: " + getRipesVersion());
    aboutDialog.exec();
}

namespace {
void writeTextFile(QFile& file, const QString& data) {
    if (file.open(QIODevice::WriteOnly)) {
        QTextStream stream(&file);
        stream << data;
        file.close();
    }
}

void writeBinaryFile(QFile& file, const QByteArray& data) {
    if (file.open(QIODevice::WriteOnly)) {
        file.write(data);
        file.close();
    }
}

}  // namespace

void MainWindow::saveFilesTriggered() {
    SaveDialog diag;
    if (!m_hasSavedFile) {
        if (diag.exec()) {
            m_hasSavedFile = true;
        }
    }

    if (!diag.assemblyPath().isEmpty()) {
        QFile file(diag.assemblyPath());
        writeTextFile(file, static_cast<EditTab*>(m_tabWidgets.at(EditTabID).tab)->getAssemblyText());
    }

    if (!diag.binaryPath().isEmpty()) {
        QFile file(diag.binaryPath());
        if (auto* program = static_cast<EditTab*>(m_tabWidgets.at(EditTabID).tab)->getBinaryData()) {
            writeBinaryFile(file, *program);
        }
    }
}

void MainWindow::saveFilesAsTriggered() {
    SaveDialog diag;
    auto ret = diag.exec();
    if (ret == QDialog::Rejected)
        return;
    m_hasSavedFile = true;
    saveFilesTriggered();
}

void MainWindow::settingsTriggered() {
    SettingsDialog diag;
    diag.exec();
}

void MainWindow::newProgramTriggered() {
    QMessageBox mbox;
    mbox.setWindowTitle("New Program...");
    mbox.setStandardButtons(QMessageBox::Yes | QMessageBox::No | QMessageBox::Cancel);
    if (!static_cast<EditTab*>(m_tabWidgets.at(EditTabID).tab)->getAssemblyText().isEmpty() || m_hasSavedFile) {
        // User wrote a program but did not save it to a file yet
        mbox.setText("Save program before creating new file?");
        auto ret = mbox.exec();
        switch (ret) {
            case QMessageBox::Yes: {
                saveFilesTriggered();
                if (!m_hasSavedFile) {
                    // User must have rejected the save file dialog
                    return;
                }
                break;
            }
            case QMessageBox::No: {
                break;
            }
            case QMessageBox::Cancel: {
                return;
            }
        }
    }
    m_hasSavedFile = false;
    static_cast<EditTab*>(m_tabWidgets.at(EditTabID).tab)->newProgram();
}

}  // namespace Ripes
