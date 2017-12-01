# -*- coding: utf-8 -*-
"""
Created on Fri Jan 27 08:44:05 2016

@author: ChenJu
"""
import time, sys
import os.path
from serial import Serial
from ConfigParser import ConfigParser


PANA_SEED='DB10E047036D446D8ED65CF857F0EB63B5574E206D4443D0BC27F8001CCF6170'
ANSI_METER_PASSWORD = '40205&94'

class MODULE_DUT:
    def __init__(self, attr):
        self.attr = attr

        print self.attr

        cfg_file = self.attr['CONFIG_FILE']
        
        if cfg_file:
            if not os.path.exists(cfg_file):
                print "Error: the config file %s does not exist!" % cfg_file
                exit(1)            
        else:
            #self.config.read('default.cfg')
            print "Error: the config file is not specified"
            exit(1)
        
        self.config = ConfigParser()
        self.config.read(cfg_file)

        
        if self.attr['LANID']:
            if len(self.attr['LANID']) != 8:
                print "\rError: Invalid LanID Length"
                exit(1)
            
            try:
                int(self.attr['LANID'], 16)
            except:
                print "\rError: Invalid LanID"
                exit(1)
        else:
            print "\rWARNING: LANID is NULL"        

        COM_PORT = self.attr['PORT']
        if COM_PORT == None:      
            COM_PORT = self.config.getint('COM', 'PORT')
        
        COM_BAUD_RATE = self.config.getint('COM', 'BAUDRATE')

        self.connect(COM_PORT, COM_BAUD_RATE)

    def connect(self, port, baud_rate):
        self.serial_port = Serial(port - 1, baud_rate, timeout=0.1)

    def disconnect(self):
        self.serial_port.close()
   
    def diag_command(self, command, response="]:", timeout=1):
        self.serial_port.flushInput()
        self.serial_port.write(command)
        
        if not timeout:
            return None
        
#        while not response in self.serial_port.readline():
#            time.sleep(0.1)
        while 1:
            output = self.serial_port.readline()
            sys.stdout.write(output)
            if response in output:
                return output
            else:
                time.sleep(0.1)

    def diagMenu(self, menu=''):
        self.diag_command('x')
        self.diag_command('x')
        if menu:
            self.diag_command(menu)
        else:
            return
            
    def diagValue(self, response):
        return response[(response.index(": ") + 2):]        
            
    def reset(self):
        self.diag_command('\r')
        self.diag_command('x')
        self.diag_command('x')
        self.diag_command('z', timeout=0)
        
    def setHardID(self):        
        #TODO: hardwareID is 0E or 8E for ANZ modules
        HARDWARE_ID = self.config.getint('Default', 'HARDWARE_ID')
        
        self.diag_command('H', response="Enter Hardware Type")
        self.diag_command('%d\r' % HARDWARE_ID)
        
    def setLanID(self):
        LAN_ID = self.attr['LANID']
        
        self.diag_command('L', response="Enter HEX Byte 1:")
        self.diag_command('%x\r' % int(LAN_ID[0:2], 16), response="Enter HEX Byte 2:")
        self.diag_command('%x\r' % int(LAN_ID[2:4], 16), response="Enter HEX Byte 3:")
        self.diag_command('%x\r' % int(LAN_ID[4:6], 16), response="Enter HEX Byte 4:")
        self.diag_command('%x\r' % int(LAN_ID[6:8], 16))
        
    def setSoftID(self):
        soft_id = self.config.get('Default', 'SoftID')
        self.diag_command('S', response="Enter Soft Id (0-65535):")
        self.diag_command('%d\r' % int(soft_id, 10))

    def setTimeZone(self):
        time_zone =  self.config.get('Default', 'TimeZone')
        self.diag_command('U', response="Enter GMTOffset(HEX):")
        self.diag_command('%x\r' % int(time_zone, 10))

    def setMSN(self):
        MSN = self.config.get('Default', 'MSN')        
        self.diag_command('M', response = "Enter MSN Value")
        self.diag_command('%s\r' % MSN)
        
    def setNetworkCRC(self):
        NetworkCRC = self.config.get('Default', 'NetworkCRC')
        
        self.diag_command('C', response="Enter CRC Adder")
        self.diag_command('%x\r' % int(NetworkCRC, 10))
        
    def disableEncryption(self):
        ENCRYPTION = self.config.getint('Default', 'ENCRYPTION')
        if not ENCRYPTION:
            self.diag_command('e')

    def setPSEMX(self):        
        ENABLE_PSEMX = self.attr['PSEMX']
        if ENABLE_PSEMX == None:
            ENABLE_PSEMX = self.config.get('Default', 'ENABLE_PSEMX')
            
        self.diag_command('E', response="Use PSEMX Protocol?")
        
        if ENABLE_PSEMX == 'y':
            self.diag_command('%s' % ENABLE_PSEMX, response='Enter PSEMX User Password:')   
            
            USER_LVL_3_PASSWORD = self.config.get('Default', 'USER_LVL_3_PASSWORD')
            self.diag_command('%s' % USER_LVL_3_PASSWORD, response='Enter PSEMX User Key:')   
            
            USER_LVL_3_KEY = self.config.get('Default', 'USER_LVL_3_KEY')
            self.diag_command('%s' % USER_LVL_3_KEY, response='Security Mode')     
        else:
            self.diag_command('N')

    def setAnsiPassword(self):       
        self.diag_command('M', response="Enter Meter Password Value(ESC to view):")
        self.diag_command('%s\r' % ANSI_METER_PASSWORD)

    def setPanaSeed(self):
        self.diag_command('S', response="Enter PANA Seed (ESC to view CRC's):")
        self.diag_command(PANA_SEED)        

    # RF modules:    
    def setConfigBytes(self):
        self.diag_command('B',    response="Enter C1(HEX):")
        self.diag_command('69\r', response="Enter C2(HEX):")
        self.diag_command('68\r', response="Enter C3(HEX):")
        self.diag_command('00\r', response="Enter C4(HEX):")
        self.diag_command('48\r', response="Enter C5(HEX):")
        self.diag_command('00\r', response="Enter C6(HEX):")
        self.diag_command('A2\r')

    def setCountryCode(self):
        CountryCode = self.config.get('Default', 'Country')        
        self.diag_command('M', response="Enter Channel Mask (0-255):")
        self.diag_command('%d\r' % int(CountryCode, 10))        
        
    # Cellular modules:    
    def setCellular(self):
        self.diag_command('W', 'Enter pins to write:')
        self.diag_command('4\r', 'Enter value to write:')
        self.diag_command('4\r')
        time.sleep(5)             # Have to wait 5 seconds for the Cellular module to power up
        self.diag_command('I')
        
       #"Transparent mode" add two new arguments in Diagnostic menu
    def setNetwork(self):
        HES0_IP = self.config.get('Default', 'HES0_IP').split('.')
        self.diag_command('F', response="Enter HEX Byte 1:")
        self.diag_command('%x\r' % int(HES0_IP[0]), response="Enter HEX Byte 2:")
        self.diag_command('%x\r' % int(HES0_IP[1]), response="Enter HEX Byte 3:")
        self.diag_command('%x\r' % int(HES0_IP[2]), response="Enter HEX Byte 4:")
        self.diag_command('%x\r' % int(HES0_IP[3]), response='Enter HES Server Port')
        
        HES_SERVER_PORT = self.config.getint('Default', 'HES_SERVER_PORT')
        self.diag_command('%d\r' % HES_SERVER_PORT, response='Enter HES Client Port')
        
        HES_CLIENT_PORT = self.config.getint('Default', 'HES_CLIENT_PORT')
        self.diag_command('%d\r' % HES_CLIENT_PORT, response='Enter HEX Byte 1:')
        
        HES1_IP = self.config.get('Default', 'HES1_IP').split('.')
        self.diag_command('%x\r' % int(HES1_IP[0]), response="Enter HEX Byte 2:")
        self.diag_command('%x\r' % int(HES1_IP[1]), response="Enter HEX Byte 3:")
        self.diag_command('%x\r' % int(HES1_IP[2]), response="Enter HEX Byte 4:")                
        self.diag_command('%x\r' % int(HES1_IP[3]), response='Enter HEX Byte 1:')
        
        #Transparent mode
        #self.diag_command('%x\r' % int(HES1_IP[3]), response='Enter TCP Server Listen Port (0-65535)')
        #self.diag_command('0\r', response='Enter HEX Byte 1:')
        
        NTP0_IP = self.config.get('Default', 'NTP0_IP').split('.')
        self.diag_command('%x\r' % int(NTP0_IP[0]), response="Enter HEX Byte 2:")
        self.diag_command('%x\r' % int(NTP0_IP[1]), response="Enter HEX Byte 3:")
        self.diag_command('%x\r' % int(NTP0_IP[2]), response="Enter HEX Byte 4:")
        self.diag_command('%x\r' % int(NTP0_IP[3]), response="Enter HEX Byte 1:")
        
        NTP1_IP = self.config.get('Default', 'NTP1_IP').split('.')
        self.diag_command('%x\r' % int(NTP1_IP[0]), response="Enter HEX Byte 2:")
        self.diag_command('%x\r' % int(NTP1_IP[1]), response="Enter HEX Byte 3:")
        self.diag_command('%x\r' % int(NTP1_IP[2]), response="Enter HEX Byte 4:")
        self.diag_command('%x\r' % int(NTP1_IP[3]), response="Enter HB Server Port") 
        
        HB_SERVER_PORT = self.config.getint('Default', 'HB_SERVER_PORT')
        self.diag_command('%d\r' % HB_SERVER_PORT, response='Enter HB Client Port')
        
        HB_CLIENT_PORT = self.config.getint('Default', 'HB_CLIENT_PORT')
        self.diag_command('%d\r' % HB_CLIENT_PORT, response='Enter HB Rate in seconds')
        
        HB_RATE = self.config.getint('Default', 'HB_RATE')
        self.diag_command('%d\r' % HB_RATE, response='Enter HB Failover Threshold count')
        
        HB_FAILOVER = self.config.getint('Default', 'HB_FAILOVER')
        self.diag_command('%d\r' % HB_FAILOVER, response='Enter APN Value')
        
        APN = self.config.get('Default', 'APN')
        self.diag_command('%s\r' % APN)        
        
        #Transparent mode
        #self.diag_command('%s\r' % APN, response='Enter HEX Band Mask') 
        #Enter HEX Band Mask:
        #self.diag_command('\r')