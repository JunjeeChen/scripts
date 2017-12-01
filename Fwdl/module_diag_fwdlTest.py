
import time, sys
import os
import struct
from serial import Serial
from shutil import copy2

from utils import crcBufferSlow

COM_PORT=7
COM_BAUD_RATE=115200
NEW_IMAGE="./tmp_fw_image.bin"

CHUNCK_SIZE=256
FWDL_FIRST_PKT=int('00000001', 2)
FWDL_LAST_PKT=int('00000010',  2)

# Ack code
FWDL_UART_OK        = 0
FWDL_UART_BAD_START = 1
FWDL_UART_BAD_LEN   = 2
FWDL_UART_BAD_CRC   = 3
FWDL_UART_BAD_IMG   = 4

NMP_HEADER_READ     = 128
NMP_DEV_TYPE_OFFSET = 23
NMP_PROM_OFFSET     = 108
NMP_SEQ_OFFSET      = 25

IMAGE_FOR_MODULE    = 1
IMAGE_FOR_METER     = 5

from module_diag_config import MODULE_DUT

class DIAG_FWDL(MODULE_DUT):
    def __init__(self):
        print ("DIAG_FWDL Initiate...")

    def dumpHexData(self, data):
        i = 0
        for byte in data:
            i = i + 1
            # print(hex(ord(byte))),
            print "%02X" % ord(byte),  # print without newline
            if (i % 16) == 0:
                print

    def fwdl_write_pkt(self, data):
        self.serial_port.flushInput()
        self.serial_port.flushOutput()

        self.serial_port.write(data)

        # Wait for the ack
        while True:
            ack = self.serial_port.read()
            #self.serial_port.flushInput()
            if len(ack) != 0:
                break

        return ord(ack)

    def build_fwdl_pkt(self, offset, data_len, data, flag=0):
        '''
        <Start_1B><Reserved_1B><Offset_4B><Len_2B><CRC_2B><Data>
        Len: the length of the Data field
        Big-endian
        '''

        send_data = b'\xEF' + struct.pack('B', flag) + struct.pack('>IH', offset, data_len)
        crc = crcBufferSlow(bytearray(data), 0xFFFF, 0x8408)
        send_data += struct.pack('>H', crc)
        send_data += data

        print "Snd: flag=0x%x, offset=0x%04x, len=0x%02x, crc=0x%02x" % (flag, offset, data_len, crc)
        #self.dumpHexData(send_data)
        return send_data

    def convert_binary_file(self, filename):
        try:
            copy2(filename, NEW_IMAGE)

            file = open(NEW_IMAGE, mode='r+')
            file.seek(0)
            file_buf = file.read(128)

            device_type = struct.unpack('B', file_buf[NMP_DEV_TYPE_OFFSET])[0]  #struct.unpack returns tuple
            if device_type==IMAGE_FOR_MODULE:
                # Get firmware version and replace the partnum in header of tmp file
                # For S5SM2D, "[S5SM2D-12.57.J04] 1024/1024KB(CPU-F) 256KB(SRAM) 4096KB(SF) 0KB(E-SRAM)"
                self.diagMenu('M')
                output = self.diag_command('F', response="[")
                cur_partnum = output[output.find('[')+1 : output.find(']')]

                # Increase the Sequence number
                seq_nbr = struct.unpack('<I', file_buf[NMP_SEQ_OFFSET:NMP_SEQ_OFFSET+4])[0] + 1

                #print "[%s], Seq=%d" % (cur_partnum, seq_nbr)

                # Update the file header with new value
                file_buf = file_buf[:NMP_PROM_OFFSET] + cur_partnum + file_buf[NMP_PROM_OFFSET+16:]
                file_buf = file_buf[:NMP_SEQ_OFFSET] + struct.pack('<I', seq_nbr) + file_buf[NMP_SEQ_OFFSET+4:]

                #self.dumpHexData(file_buf)

                file.seek(0)
                file.write(file_buf)
        finally:
            file.close()

    def fwdl_test(self, filename):
        # Change the NMP header for Module image file
        self.convert_binary_file(filename)

        # Write new image file to module
        self.diagMenu('F')
        self.diag_command('F', response='F', timeout=1)

        try:
            binary_file = open(NEW_IMAGE, mode='rb')
            file_size = os.stat(NEW_IMAGE).st_size
            #print "file_siez: 0x%04x" % file_size
            offset = 0

            while  offset < file_size:
                #print '.',

                # Read out the binary data from the image file
                # TODO:
                # 1. do not use fixed size here
                # 2. Check the size of bytes_read
                binary_file.seek(offset)
                bytes_read = binary_file.read(CHUNCK_SIZE)

                # Set the flag
                if offset == 0:
                    flag = FWDL_FIRST_PKT
                elif (offset + CHUNCK_SIZE) == file_size:
                    flag = FWDL_LAST_PKT
                elif (offset + file_size%CHUNCK_SIZE) == file_size:
                    flag = FWDL_LAST_PKT
                else:
                    flag = 0

                #print 'Progress: %f' % ((float(offset)*100)/file_size)

                # Build the packet that send to module through debug port
                snd_pkt = self.build_fwdl_pkt(offset, len(bytes_read), bytes_read, flag)

                # Write the encoded packet to debug port
                while True:
                    write_result = self.fwdl_write_pkt(snd_pkt)
                    if write_result == FWDL_UART_OK:
                        break
                    elif write_result == FWDL_UART_BAD_IMG:
                        raise Exception('Module could not recognize this image file!')
                    else:
                        print "Retry result=0x%x, Offset=0x%x" % (write_result, offset)

                offset += CHUNCK_SIZE
                #print "offset=%d, file_size=%d" % (offset, file_size)

                if flag == FWDL_LAST_PKT:
                    break

        finally:
            print "Complete write image to module!"
            binary_file.close()
            os.remove(NEW_IMAGE)



if __name__=='__main__':
    attr = {'PORT':        COM_PORT,
            'BAUD_RATE':   COM_BAUD_RATE,
            'IMAGE':       NEW_IMAGE}

    dut = DIAG_FWDL()

    dut.connect(attr['PORT'], attr['BAUD_RATE'])

    #dut.diagMenu('F')
    dut.fwdl_test(attr['IMAGE'])
    #dut.reset()
    dut.disconnect()
