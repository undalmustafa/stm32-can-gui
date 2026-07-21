"""Application-wide visual theme for the desktop diagnostic console."""


APP_STYLE_SHEET = """
QWidget {
    background: #f4f6f8;
    color: #1d2939;
    font-size: 10pt;
}

QLabel#appTitle {
    font-size: 17pt;
    font-weight: 700;
    color: #101828;
}

QLabel#appContext, QLabel#activityCount, QLabel#statusMeta {
    color: #667085;
}

QLabel#statusMeta {
    font-size: 9pt;
}

QLabel#statusSummary, QLabel#healthSummary {
    font-weight: 700;
}

QGroupBox {
    background: #ffffff;
    border: 1px solid #d0d5dd;
    border-radius: 6px;
    font-weight: 600;
    margin-top: 12px;
    padding-top: 10px;
}

QGroupBox::title {
    subcontrol-origin: margin;
    left: 10px;
    padding: 0 4px;
    color: #344054;
}

QLineEdit, QSpinBox, QComboBox {
    background: #ffffff;
    border: 1px solid #98a2b3;
    border-radius: 4px;
    min-height: 28px;
    padding: 1px 7px;
    selection-background-color: #2e6f8e;
}

QLineEdit:focus, QSpinBox:focus, QComboBox:focus {
    border: 2px solid #2e6f8e;
}

QPushButton {
    background: #ffffff;
    border: 1px solid #98a2b3;
    border-radius: 4px;
    min-height: 30px;
    padding: 1px 12px;
    font-weight: 600;
}

QPushButton:hover {
    background: #eef4f7;
    border-color: #2e6f8e;
}

QPushButton:pressed {
    background: #dce9ee;
}

QPushButton#primaryButton {
    background: #20617d;
    border-color: #20617d;
    color: #ffffff;
}

QPushButton#primaryButton:hover {
    background: #174d65;
}

QTabWidget::pane {
    border: 1px solid #d0d5dd;
    background: #f4f6f8;
    top: -1px;
}

QTabBar::tab {
    background: #e7ebef;
    border: 1px solid #d0d5dd;
    padding: 9px 18px;
    min-width: 105px;
}

QTabBar::tab:selected {
    background: #ffffff;
    border-top: 3px solid #2e6f8e;
    padding-top: 7px;
    color: #174d65;
    font-weight: 700;
}

QTabBar::tab:hover:!selected {
    background: #dde4e9;
}

QTableWidget {
    background: #ffffff;
    alternate-background-color: #f8fafc;
    border: 1px solid #d0d5dd;
    border-radius: 4px;
    selection-background-color: #dce9ee;
    selection-color: #101828;
}

QHeaderView::section {
    background: #e7ebef;
    border: 0;
    border-right: 1px solid #d0d5dd;
    border-bottom: 1px solid #d0d5dd;
    color: #344054;
    font-weight: 700;
    padding: 7px;
}

QScrollArea#pageScroll {
    background: #f4f6f8;
}

QToolTip {
    background: #101828;
    color: #ffffff;
    border: 1px solid #344054;
    padding: 4px;
}
"""


def apply_application_theme(application):
    application.setStyle("Fusion")
    application.setStyleSheet(APP_STYLE_SHEET)
