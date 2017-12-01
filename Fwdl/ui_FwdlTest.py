# -*- coding: utf-8 -*-

# Form implementation generated from reading ui file 'FwdlTest.ui'
#
# Created: Mon Nov 27 14:23:37 2017
#      by: pyside-uic 0.2.15 running on PySide 1.2.4
#
# WARNING! All changes made in this file will be lost!

from PySide import QtCore, QtGui

class Ui_MainWindow(object):
    def setupUi(self, MainWindow):
        MainWindow.setObjectName("MainWindow")
        MainWindow.resize(503, 286)
        self.centralwidget = QtGui.QWidget(MainWindow)
        self.centralwidget.setObjectName("centralwidget")
        self.pushButton_upgrade = QtGui.QPushButton(self.centralwidget)
        self.pushButton_upgrade.setGeometry(QtCore.QRect(30, 190, 75, 23))
        font = QtGui.QFont()
        font.setPointSize(10)
        self.pushButton_upgrade.setFont(font)
        self.pushButton_upgrade.setObjectName("pushButton_upgrade")
        self.comboBox_port = QtGui.QComboBox(self.centralwidget)
        self.comboBox_port.setGeometry(QtCore.QRect(100, 30, 69, 22))
        self.comboBox_port.setObjectName("comboBox_port")
        self.comboBox_baudrate = QtGui.QComboBox(self.centralwidget)
        self.comboBox_baudrate.setGeometry(QtCore.QRect(100, 70, 69, 22))
        self.comboBox_baudrate.setObjectName("comboBox_baudrate")
        self.label_port = QtGui.QLabel(self.centralwidget)
        self.label_port.setGeometry(QtCore.QRect(30, 30, 51, 16))
        self.label_port.setObjectName("label_port")
        self.label_baudrate = QtGui.QLabel(self.centralwidget)
        self.label_baudrate.setGeometry(QtCore.QRect(30, 70, 51, 16))
        self.label_baudrate.setObjectName("label_baudrate")
        self.label_file = QtGui.QLabel(self.centralwidget)
        self.label_file.setGeometry(QtCore.QRect(30, 130, 46, 13))
        self.label_file.setObjectName("label_file")
        self.lineEdit_file = QtGui.QLineEdit(self.centralwidget)
        self.lineEdit_file.setGeometry(QtCore.QRect(100, 130, 231, 20))
        self.lineEdit_file.setObjectName("lineEdit_file")
        self.pushButton_fileSelect = QtGui.QPushButton(self.centralwidget)
        self.pushButton_fileSelect.setGeometry(QtCore.QRect(350, 130, 75, 23))
        font = QtGui.QFont()
        font.setPointSize(10)
        self.pushButton_fileSelect.setFont(font)
        self.pushButton_fileSelect.setObjectName("pushButton_fileSelect")
        MainWindow.setCentralWidget(self.centralwidget)
        self.menubar = QtGui.QMenuBar(MainWindow)
        self.menubar.setGeometry(QtCore.QRect(0, 0, 503, 21))
        self.menubar.setObjectName("menubar")
        MainWindow.setMenuBar(self.menubar)
        self.statusbar = QtGui.QStatusBar(MainWindow)
        self.statusbar.setObjectName("statusbar")
        MainWindow.setStatusBar(self.statusbar)

        self.retranslateUi(MainWindow)
        QtCore.QMetaObject.connectSlotsByName(MainWindow)

    def retranslateUi(self, MainWindow):
        MainWindow.setWindowTitle(QtGui.QApplication.translate("MainWindow", "FwdlTest", None, QtGui.QApplication.UnicodeUTF8))
        self.pushButton_upgrade.setText(QtGui.QApplication.translate("MainWindow", "upgrade", None, QtGui.QApplication.UnicodeUTF8))
        self.label_port.setText(QtGui.QApplication.translate("MainWindow", "<html><head/><body><p><span style=\" font-size:10pt;\">Port</span></p></body></html>", None, QtGui.QApplication.UnicodeUTF8))
        self.label_baudrate.setText(QtGui.QApplication.translate("MainWindow", "<html><head/><body><p><span style=\" font-size:10pt;\">Baudrate</span></p></body></html>", None, QtGui.QApplication.UnicodeUTF8))
        self.label_file.setText(QtGui.QApplication.translate("MainWindow", "<html><head/><body><p><span style=\" font-size:10pt;\">File</span></p></body></html>", None, QtGui.QApplication.UnicodeUTF8))
        self.pushButton_fileSelect.setText(QtGui.QApplication.translate("MainWindow", "select", None, QtGui.QApplication.UnicodeUTF8))

