import sys, os
from PySide import QtCore, QtGui
from serial.tools.list_ports import comports

from ui_FwdlTest import Ui_MainWindow
from module_diag_fwdlTest import DIAG_FWDL

BAUD_RATE = {0:9600, 1:38400, 2:19200, 3:115200}

class FwdlTest(QtGui.QMainWindow, Ui_MainWindow):
    def __init__(self):
        QtGui.QMainWindow.__init__(self)
        self.setupUi(self)

        self.pushButton_fileSelect.clicked.connect(self.pushButton_fileSelect_clicked)
        self.pushButton_upgrade.clicked.connect(self.pushButton_upgrade_clicked)

    def pushButton_fileSelect_clicked(self):
        path, filter = self.fileDialog.getOpenFileName(self, "Select binary file", ".")
        if path:
            self.binaryFile = path
            self.lineEdit_file.setText(self.binaryFile)

    def pushButton_upgrade_clicked(self):
        self.dut = DIAG_FWDL()
        self.com_port = int(self.comboBox_port.currentText()[3:])
        self.baudrate = BAUD_RATE[self.comboBox_baudrate.currentIndex()]

        if os.path.isfile(self.lineEdit_file.text()):
            self.dut.connect(self.com_port, self.baudrate)
            self.dut.fwdl_test(self.binaryFile)
            self.dut.disconnect()

            QtGui.QMessageBox().information(self, "Info", "The new image has been sent to module!")
        else:
            QtGui.QMessageBox().critical(self, "Error!", "This file does not exist!")

    #def debugOut(self, msg):
    #    self.textEdit_Debug.append(msg)

if __name__ == "__main__":
    app = QtGui.QApplication(sys.argv)

    window = FwdlTest()

    # Get the available COM ports
    com_ports = []
    for com_port in list(comports()):
        com_ports.append(com_port[0])

    # com_ports ['COM3', 'COM4', 'COM5', 'COM7', 'COM8']
    com_ports.sort()
    #print com_ports

    # Display the COM ports
    for i in range(len(com_ports)):
        window.comboBox_port.addItem("")
        window.comboBox_port.setItemText(i, QtGui.QApplication.translate("MainWindow", com_ports[i], None,
                                                                         QtGui.QApplication.UnicodeUTF8))
    # Display the Baudrates
    for i in range(len(BAUD_RATE)):
        window.comboBox_baudrate.addItem("")
        window.comboBox_baudrate.setItemText(i, QtGui.QApplication.translate("MainWindow", str(BAUD_RATE[i]), None,
                                                                            QtGui.QApplication.UnicodeUTF8))

    window.fileDialog = QtGui.QFileDialog(window)

    window.show()
    sys.exit(app.exec_())
