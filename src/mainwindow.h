#pragma once
#include <QtWidgets/QMainWindow>
#include <QDate>
#include "ui_mainwindow.h"
#include "dbmanager.h"
#include "fileindexer.h"
#include "searchengine.h"

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    MainWindow(QWidget* parent = nullptr);
    ~MainWindow();

private slots:
    void on_pushButtonBrowse_clicked();
    void on_pushButtonScan_clicked();
    void on_pushButtonSearch_clicked();
    void on_tableWidgetResults_customContextMenuRequested(const QPoint& pos);
    void on_actionClearIndex_triggered();
    void on_actionExit_triggered();

private:
    Ui::MainWindowClass ui;  // ? объект (VS шаблон)
    DBManager           m_db;

    void setupResultsTable();
    void fillResults(const QVector<SearchResult>& rows,
        const QString& query, bool caseSensitive);

    static void setHighlightedText(QTableWidget* table, int row, int col,
        const QString& text,
        const QString& query,
        Qt::CaseSensitivity cs);
};