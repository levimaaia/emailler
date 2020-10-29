# Apple II Email and Usenet News Suite

<p align="center"><img src="img/emailler-logo.png" alt="emai//er-logo" height="200px"></p>

## System Setup and Configuration

### Configuration File `EMAIL.CFG`

The system configuration file is called `EMAIL.CFG`.  It is a straightforward ProDOS text file, with one parameter per line.  You may edit this file using the provided editor, `EDIT.SYSTEM` (or any other ProDOS text editor).  When editing the file be careful not to add or delete any lines - this file has no grammar and the lines *must* appear in the expected order.

To edit the file using `EDIT.SYSTEM`:

  - Run `EDIT.SYSTEM` using Bitsy Bye or your usual ProDOS launcher.
  - Press Open Apple-O to open a file, then enter `EMAIL.CFG` at the prompt, followed by return.
  - Editing is fairly intuitive.  Use the arrow keys to move around and type to insert text.  Open Apple-Delete deletes to the right.
  - When you are satisfied, save the file using Open Apple-S.
  - Quit the editor using Open Apple-Q.

All three of the mail programs `POP65.SYSTEM`, `EMAIL.SYSTEM` and `SMTP65.SYSTEM` share this configuration file.

Here is an example config file (with passwords replaced with `****` for obvious reasons):

```
192.168.10.2:110
pi
******
NODELETE
192.168.10.2:25
apple2.local
/IP65
/H1/EMAIL
bobbi.8bit@gmail.com
```

The lines are as follows, in order:

 1) IP address of the POP3 server for receiving new mail, optionally followed by a colon and then the TCP port number.  If the colon and port number are omitted, port 110 is the default.
 2) Username to use when connecting to POP3.
 3) Password for POP3 connection (in plaintext).
 4) If this string is exactly `DELETE` then messages will be deleted from the POP3 server after downloading.  Otherwise they are left on the server.  `DELETE` is the normal setting, but `NODELETE` (or any other nonsense value) can be helpful for debugging, allowing the same messages to be downloaded from the POP3 server again and again.
 5) IP address of the SMTP server for sending outgoing mail, optionally followed by a colon and then the TCP portnumber.  If the colon and port number are omitted, port 25 is the default.
 6) Domain name that is passed to the SMTP server on connection.  The way my SMTP server (Postfix) is configured, it doesn't seem to care about this.
 7) ProDOS path of the directory where the email executables are installed.
 8) ProDOS path to the root of the email folder tree.  Mailboxes will be created and managed under this root path.
 9) Your email address.  Used as the sender's address in outgoing messages.

### Creating Directories

To get started, you will need to create the following directories:

 - The email root directory (`/H1/DOCUMENTS/EMAIL` in the example config)
 - The `SPOOL` directory, used by POP65, within the email root directory.  This will be `/H1/DOCUMENTS/EMAIL/SPOOL` for our example configuration.
 - The `INBOX` directory, used by POP65, within the email root directory.  This will be `/H1/DOCUMENTS/EMAIL/INBOX` for our example configuration.
 - The `OUTBOX` directory, used by SMTP65, within the email root directory.  This will be `/H1/DOCUMENTS/EMAIL/OUTBOX` for our example configuration.
 - The `ATTACHMENTS` directory, used by EMAIL for storing downloaded MIME attachments, within the email root directory.  This will be `/H1/DOCUMENTS/EMAIL/ATTACHMENTS` for our example configuration.

You can create these directories in ProDOS `BASIC.SYSTEM` as follows:

```
] CREATE /H1/DOCUMENTS/EMAIL
] CREATE /H1/DOCUMENTS/EMAIL/SPOOL
] CREATE /H1/DOCUMENTS/EMAIL/INBOX
] CREATE /H1/DOCUMENTS/EMAIL/OUTBOX
] CREATE /H1/DOCUMENTS/EMAIL/ATTACHMENTS
```

### Creating Mailboxes

You will also want to create a couple of mailboxes such as `RECEIVED` and `SENT`.  If you do not create a `SENT` mailbox then `SMTP65.SYSTEM` will be unable to complete the sending of messages and will give an error.  To create these mailboxes, run `EMAIL.SYSTEM` and press `N` for N)ew mailbox.  At the prompt, enter the name of the mailbox to be created: `RECEIVED`, and press return.  Repeat this to create the `SENT` mailbox.

These are the minimum mailboxes you need to get started.  You may create more mailboxes to organize your mail at any time.

### First Run of `POP65.SYSTEM`

The first time `POP65.SYSTEM` runs and downloads mail messages it will populate the `INBOX`.  However the `INBOX` directory needs to have been created already (as described above.)

Note: If you try to run `EMAIL.SYSTEM` before the `POP65.SYSTEM` has initialized the INBOX, an error will be reported about a missing file.

Once you have correctly configured `EMAIL.CFG` with the correct POP3 server, username and password, start `POP65.SYSTEM` using Bitsy Bye or your favourite ProDOS launcher.  It should connect to your POP3 server, download any messages in your email inbox and copy them to your `INBOX`.

Once messages have been downloaded and the `INBOX` initialized, you may run `EMAIL.SYSTEM` to browse and read the new messages.

