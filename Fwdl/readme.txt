How to do firmwawre download over the Debug port:
#1 Program the module by ProgramMCE_SAM4C32.bat with the module firmware supports this feature
#2 Run config.bat to configure the modue in Diagnostic mode
#3 Run the "module_diag_fwdlTest.py" to write the new image from PC to module external flash through UART port
#4 Reboot the module and run into Operational mode 
#5 Module firmware automatically upgrade itself or write new image to meter, which depends on the dev_type

For the module firmware download, we need to change the original binary file
#1 Set content of NMP_PROM (Offset 108, :Len 22B) as the current firmware partnumber running on the module
#2 Change the SeqNum (Offset 25, Len 4B) to different value