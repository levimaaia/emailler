# Apple II Email Suite

<p align="center"><img src="img/emailler-logo.png" alt="emai//er-logo" height="200px"></p>

The following programs are completely new:

 - `POP65` is a Post Office Protocol version 3 (POP3) client for the Apple II with Uthernet-II card.
 - `EMAIL` is a simple user interface for reading and managing email.  It works together with `POP65` and `SMTP65`.
 - `SMTP65` is a Simple Mail Transport Protocol (SMTP) client for the Apple II with Uthernet-II card.

## Overview

## System Setup and Configuration

### Configuration File

The system configuration file is called `POP65.CFG`.  It is a straightforward ProDOS text file, with one parameter per line.  You may edit this file using any ProDOS text editor.  When editing the file be careful not to add or delete any lines - this file has no grammar and the lines *must* appear in the expected order.

All three of the mail programs `POP65.SYSTEM`, `EMAIL.SYSTEM` and `SMTP65.SYSTEM` share this configuration file.

Here is an example config file (with passwords replaced with `****` for obvious reasons):

```
192.168.10.2
pi
******
NODELETE
192.168.10.2
apple2.local
/H1/DOCUMENTS/EMAIL
bobbi.8bit@gmail.com
```

The lines are as follows, in order:

 1) IP address of the POP3 server for receiving new mail.
 2) Username to use when connecting to POP3.
 3) Password for POP3 connection (in plaintext).
 4) If this string is exactly `DELETE` then messages will be deleted from the POP3 server after downloading.  Otherwise they are left on the server.  `DELETE` is the normal setting, but `NODELETE` (or any other nonsense value) can be helpful for debugging, allowing the same messages to be downloaded from the POP3 server again and again.
 5) IP address of the SMTP server for sending outgoing mail.
 6) Domain name that is passed to the SMTP server on connection.  The way my SMTP server (Postfix) is configured, it doesn't seem to care about this.
 7) ProDOS path to the root of the email folder tree.  Mailboxed will be created and managed under this root path.
 8) Your email address.  Used as the sender's address in outgoing messages.

### Creating Directories

To get started, you will need to create the following directories:

 - The email root directory (`/H1/DOCUMENTS/EMAIL` in the example config)
 - The `SPOOL` directory, used by POP65, within the email root directory.  This will be `/H1/DOCUMENTS/EMAIL/SPOOL` for our example configuration.

You can create these directories in ProDOS `BASIC.SYSTEM` as follows:

```
] CREATE /H1/DOCUMENTS/EMAIL
] CREATE /H1/DOCUMENTS/EMAIL/SPOOL
] CREATE /H1/DOCUMENTS/EMAIL/INBOX
```

### Mailboxes

Each mailbox consists of the following:

 - A directory under the email root, and within this directory
 - Optionally, email messages in plain text files named `EMAIL.nn` where `nn` is an integer value
 - A text file called `NEXT.EMAIL`.  This file initially contains the number 1.  It is used when naming the individual `EMAIL.nn` files, and is incremented by one each time.  If messages are added to a mailbox and nothing is ever deleted they will be sequentially numbered `EMAIL.1`, `EMAIL.2`, etc.
 - A binary file called `EMAIL.DB`.  This file contains essential information about each email message in a quickly accessed format.  This allows the user interface to show the email summary without having to open and read each individual email file.  This file is initially empty and a fixed size record is added for each email message.

The easiest way to create additional mailboxes is using the `N)ew` command in `EMAIL.SYSTEM`.

`POP65.SYSTEM` knows how to initialize `INBOX` but the directory must have been created first.

## `POP65.SYSTEM`

<p align="center"><img src="img/POP65.jpg" alt="Summary Screen" height="400px"></p>

POP65 is a Post Office Protocol v3 (POP3) client for the Apple II.  It requires an Uthernet-II ethernet card and will not work with other interfaces without modification, because it uses the W5100 hardware TCP/IP stack.  POP65 is used to download new email messages from a POP3 email server.  (I use Dovecot on the Raspberry Pi as my POP3 server, but other POP3 servers should work too.)

Before running `POP65.SYSTEM` for the first time, be sure you have created the email root directory and the `SPOOL` directory, as described above.  POP3 will initialize the `INBOX` mailbox, creating `NEXT.EMAIL` and `EMAIL.DB` files if they do not exist.

POP65 runs without any user interaction and performs the following tasks:

 - Detect Uthernet-II
 - Obtain IP address using DHCP
 - Connect to POP3 server using parameters from first three lines of `POP65.CFG`. (`USER` and `PASS` commands)
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

## `EMAIL.SYSTEM`

EMAIL is a simple mail user agent for reading and managing email.

<p align="center"><img src="img/summary-screen.png" alt="Summary Screen" height="400px"></p>

When the EMAIL application is started it will show the `INBOX` in the summary screen.  This shows the following important information for each message:

  - Tag - Shows `T` if the message is tagged.
  - Read/Unread/Deleted - Shows `*` if the message is new (unread).  Shows `D` if the message is markedto be deleted.
  - From, To, Date and Subject) for 18 messages at a time.

Main menu commands:

 - Up arrow / `K` - Move the selection to the previous message. If this is the first message on the summary screen but this is not the first page, then load the previous page of messages and select the last item.
 - Down arrow / `J` - Move the selection to the next message.  If this is the last message on the summary screen but there are further messages on subsequent pages, then load the next page of messages and select the first item.
 - `SPC` / `RET` - View the currently selected message in the message pager.
 - `S)witch` mbox - Switch to viewing a different mailbox. Press `S` then enter the name of the mailbox to switch to at the prompt.  The mailbox must already exist or an error message will be shown.  You may enter `.` as a shortcut to switch back to `INBOX`.
 - `N)ew mbox` - Create a new mailbox.  Press 'N' then enter the name of the mailbox to be created.  It will be created as a directory within the email root directory and `NEXT.EMAIL` and `EMAIL.DB` files will be created for the new mailbox.
 - `C)opy` - Copy message(s) to another mailbox.  If no messages are tagged (see below) then the copy operation will apply to the current message only.  If messages are tagged then the copy operation will apply to the tagged messages.
 - `M)ove` - Move message(s) to another mailbox. If no messages are tagged (see below) then the move operation will apply to the current message only.  If messages are tagged then the copy operation will
 apply to the tagged messages.  Moving a message involves two steps - first the message is copied to the destination mailbox and then it is marked as deleted in the source mailbox.
 - `A)rchive` - This is a shortcut for moving messages to the `RECEIVED` mailbox.
 - `D)el` - Mark message as deleted.
 - `U)ndel` - Remove deleted mark from a message.
 - `P)urge` - Purge deleted messages from the mailbox.  This command iterates through all the messages marked for deletion and removes their files from the mailbox.  A new `EMAIL.DB` is created, compacting any 'holes' where files have been deleted.
 - `T)ag` - Toggle tag on message for collective `C)opy`, `M)ove` and `A)rchive` operations.  Moves to the next message automatically to allow rapid tagging of messages.
 - `W)rite` - Prepare a new blank outgoing email and place it in `OUTBOX` ready for editing.
 - `R)eply` - Prepare a reply to the selected email and place it in `OUTBOX` ready for editing.
 - `F)orward` - Prepare a forwarded copy of the selected email and place it in `OUTBOX` ready for editing.
 - `Q)uit` - Quit from the EMAIL user interface.

<p align="center"><img src="img/email-pager.png" alt="Email Pager" height="400px"></p>

### Design Principles

...

### Persistence of Tags and Read/Unread/Deleted Status

...

### Deletion of Messages

...

### Tagging of Messages

...

### Sending of Messages

...

## `SMTP65.SYSTEM`

<p align="center"><img src="img/SMTP65.jpg" alt="Summary Screen" height="400px"></p>

SMTP65 is a Simple Mail Transport Protocol (SMTP65) client for the Apple II.  It requires an Uthernet-II ethernet card and will not work with other interfaces without modification, because it uses the W5100 hardware TCP/IP stack.  POP65 is used to send outgoing email messages to an SMTP email server.  (I use Postfix on the Raspberry Pi as my SMTP server, but other SMTP servers should work too.)

Before running SMTP65 for the first time, be sure to have created the `SENT` mailbox.  This must be a 'proper' mailbox, not just a directory.  You may create a mailbox using the `N)ew` command in `EMAIL.SYSTEM`.

SMTP65 runs without any user interaction and performs the following tasks:

 - Detect Uthernet-II
 - Obtain IP address using DHCP
 - Connect to SMTP server using parameters from lines 5 and 6 of `POP65.CFG`. (`HELO` command)
 - Iterate through each message in the `OUTBOX` mailbox (which is `/H1/DOCUMENTS/EMAIL/OUTBOX` with our sample configuration)
   - Scan each message looking for the following headers:
     - `To:`
     - `From:`
     - `cc:`
   - Notify the SMTP server of our email address (from `POP65.CFG`). (`MAIL FROM:` command)
   - Notify the SMTP server of each recipient listed in `To:` and `From:` headers (`RCPT TO:` command)
   - Send the email body to the SMTP sender. (`DATA` command)
   - If the message was successfully sent, copy it to the `SENT` mailbox.
   - Remove the sent message from `OUTBOX`.
   - Iterate until all messages in `OUTBOX` have been sent, and copied to `SENT`.  Rejected messages are left in `OUTBOX` where they may be edited and retransmitted.

