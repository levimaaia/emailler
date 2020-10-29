# Apple II Email and Usenet News Suite

<p align="center"><img src="img/emailler-logo.png" alt="emai//er-logo" height="200px"></p>

[Back to Main emai//er Docs](README-emailler.md#detailed-documentation-for-email-functions)

## `POP65.SYSTEM`

*Run using `Open Apple`-`R` in `EMAIL.SYSTEM`*

<p align="center"><img src="img/POP65.jpg" alt="POP65" height="400px"></p>

POP65 is a Post Office Protocol v3 (POP3) client for the Apple II.  It requires an Uthernet-II ethernet card and will not work with other interfaces without modification, because it uses the W5100 hardware TCP/IP stack.  POP65 is used to download new email messages from a POP3 email server.  (I use Dovecot on the Raspberry Pi as my POP3 server, but other POP3 servers should work too.)

Before running `POP65.SYSTEM` for the first time, be sure you have created the email root directory and the `SPOOL` directory, as described [here](README-emailler-setup.md).  POP3 will initialize the `INBOX` mailbox, creating `NEXT.EMAIL` and `EMAIL.DB` files if they do not exist.

POP65 runs without any user interaction and performs the following tasks:

 - Detect Uthernet-II
 - Obtain IP address using DHCP
 - Connect to POP3 server using parameters from first three lines of `EMAIL.CFG`. (`USER` and `PASS` commands)
 - Enquire how many email messages are waiting. (`STAT` command)
 - Download each email in turn (`RETR` command) and store it in the `SPOOL` directory.
 - If configured to delete messages on the POP3 server, messages are deleted after successful download (`DELE` command)
 - Once all messages have been downloaded, disconnect from the POP3 server (`QUIT` command)
 - Scan each downloaded message in the `SPOOL` directory and import the message into `INBOX`:
   - Read `INBOX/NEXT.EMAIL` to find out the next number in sequence and allocate that for the new message.
   - Copy the message from `SPOOL` to `INBOX/EMAIL.nn` (where `nn` is the next sequence number) while scanning it for the following information:
     - Sender (`From:`) header
     - Recipient (`To:`) header
     - Date and time (`Date:`) header
     - Subject (`Subject:`) header
     - Offset in bytes to start of message body
   - Store all of the information obtained from scanning the message in `INBOX/EMAIL.DB`.
   - Update `INBOX/EMAIL.nn`, incrementing the number by one.
   - Iterate until all messages in `SPOOL` are ingested into `INBOX`.

[Back to Main emai//er Docs](README-emailler.md#detailed-documentation-for-email-functions)

