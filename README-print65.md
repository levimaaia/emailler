# Apple II Email and Usenet News Suite

<p align="center"><img src="img/emailler-logo.png" alt="emai//er-logo" height="200px"></p>

[Back to Main emai//er Docs](README.md#detailed-documentation-for-email-functions)

## `PRINT65.SYSTEM`

<!-- <p align="center"><img src="img/SMTP65.jpg" alt="SMTP65" height="400px"></p> -->

`PRINT65.SYSTEM` is a utility for printing to a network-connected printer using the Hewlett Packard Jetdirect protocol.  It requires an Uthernet-II ethernet card and will not work with other interfaces without modification, because it uses the W5100 hardware TCP/IP stack.

Before running `PRINT65.SYSTEM` for the first time, use `EDIT.SYSTEM` to create a configuration file called `PRINT.CFG`.  This file consists of a single line specifying the IP address of the network printer to use, optionally followed by a colon and a port number.  If the port number is omitted it defaults to 9100.  For example:

```
192.168.10.4:9100
```

`PRINT65.SYSTEM` performs the following tasks:

 - If no filename was provided on the command line, prompt for the filename to print
 - Detect Uthernet-II
 - Obtain IP address using DHCP
 - Connect to Jetdirect printer
 - Open file
 - Send file contents to printer over TCP/IP
 - Close file
 - Disconnect

### Using Command Line Argument to Specify the File to Print

`PRINT65.SYSTEM` supports command line arguments in a way that is compatible with the Davex shell (and possibily other environments.)  In Davex you can print a file as follows:

```
print65.system /path/to/my/file
```

### HP Jetdirect

Most HP printers support Jetdirect.  I am using an HP Photosmart 7520 which supports the Jetdirect protocol on port 9100 over it's wifi connection.  Jetdirect defaults to a simple plain text mode, which we exploit here to print in 80 column text mode.

[Back to Main emai//er Docs](README.md#detailed-documentation-for-email-functions)

