# Apple II Email and Usenet News Suite

<p align="center"><img src="img/emailler-logo.png" alt="emai//er-logo" height="200px"></p>

Emai//er is an email and Usenet news software package for the Apple //e Enhanced or Apple IIgs.  An Uthernet-II ethernet card is required for sending and receiving messages.

Emai//er is implemented as a number of ProDOS executables, each of which performs one distinct function.  `EMAIL.SYSTEM` provides the main user interface, and invokes the other executables as needed.  `EMAIL.SYSTEM` is automatically reloaded when the helper program completes its function.

 - `EMAIL.SYSTEM` is a simple user interface for reading and managing email.
 - `EDIT.SYSTEM` is a full screen editor, used for composing email and news messages. It may also be used as a general purpose ProDOS text file editor.
 - `POP65.SYSTEM` is a Post Office Protocol version 3 (POP3) client for the Apple II with Uthernet-II card.  This is used to retrieving incoming email messages.
 - `SMTP65.SYSTEM` is a Simple Mail Transport Protocol (SMTP) client for the Apple II with Uthernet-II card.  This is used for sending outgoing email messages.
 - `NNTP65.SYSTEM` is a Network News Transport Protocol (NNTP) client for the Apple II with Uthernet-II card.  This is used to retrieving Usenet news messages.
 - `NNTP65UP.SYSTEM` is a Network News Transport Protocol (NNTP) client for the Apple II with Uthernet-II card.  This is used for transmitting outgoing Usenet news messages.
 - `ATTACHER.SYSTEM` is used for creating multi-part MIME messages with attached files.
 - `REBUILD.SYSTEM` is a utility for rebuilding mailbox databases, should they become corrupted.  This can also be used for bulk import of messages.
 - `DATE65.SYSTEM` is a Network Time Protocol (NTP) client which can be used for setting the system time and date if you do not have a real time clock.

The following diagram shows the various executables that form the emai//er suite and how they execute one another.  Note how `EMAIL.SYSTEM` serves as the hub from which all the other programs may be invoked.

<p align="center"><img src="img/emailler-apps.png" alt="Emai//er Executables" height="300px"></p>

## Overview

The software has been designed to be modular, which allows new protocols to be added later for handling incoming and outgoing mail.  POP3 was selected as the email download/ingest protocol because it is simple and there are many available server implementations.  SMTP was chosen as the outgoing protocol due to its almost universal adoption for this purpose.  Once again, there are many server-side implementations to choose from.

A few design principles that I have tried to apply:

  - *Simplicity* This software runs on the Apple //e and currently fits within 64KB of RAM (although I may use the 64KB of aux memory for future enhancements.)  It is important that it be as simple and small as possible.  The code is written in C using cc65, which allows more rapid evolution when compared to writing in assembly language, at the expense of larger code which uses more memory.
  - *Modularity* Where it makes sense to split the functionality into separate modules it makes sense to do so in order to make the best use of available memory.
  - *Speed* The software should make the most of the limited hardware of the Apple //e in order to allow speedy browsing of emails without needing much processor or disk activity.
  - *Avoidance of Limits* I tried to avoid the imposition of arbitrary limits to message length or the number of messages in a folder.
  - *Veracity* The software should never modify or discard information. Incoming emails are saved to disk verbatim, including all headers.  The system hides the headers when displaying the emails, but they are available for inspection or further processing.  The only change that is made to incoming messages is to convert the CR+LF line endings to Apple II CR-only line endings.

`POP65.SYSTEM` and `SMTP65.SYSTEM` are based on Oliver Schmitd's excellent IP65 TCP/IP framework (in particular they follow the design of `WGET65.SYSTEM`.)  Without IP65, this software would not have been possible.

## System Requirements

The minimum system requirements are as follows:

 - Apple //e Enhanced or Apple IIgs computer (ROM01 and ROM03 supported)
 - Uthernet-II ethernet card
 - Mass storage device such as CFFA3000, MicroDrive/Turbo or BOOTI

A CPU accelerator is recommended if you plan to handle large volumes of email or Usenet messages.

emai//er has been extensively tested using ProDOS 2.4.2. However, it should not be a problem to run it under other versions of ProDOS.

## Transport Level Security (TLS)

One problem faced by any retrocomputing project of this type is that Transport Layer Security (TLS) is endemic on today's Internet.  While this is great for security, the encryption algorithms are not feasible to implement on a 6502-based system.  In order to bridge the plain text world of the Apple II to today's encrypted Internet, I have set up a Raspberry Pi using several common open source packages as a gateway.

The Raspberry Pi uses Fetchmail to download messages from Gmail's servers using the IMAPS protocol (with TLS) and hands them off to Postfix, which is used at the system mailer (MTA) on the Pi.  I use Dovecot as a POP3 server to offer a plain text connection to the `POP65.SYSTEM` application on the Apple II.  For outgoing messages, I configured Postfix to accept plain text SMTP connections from `SMTP65.SYSTEM` on the Apple II and to relay the messages to Gmail's servers using secure SMTPS.  

  - [The configuration of the Raspberry Pi is documented here.](README-gmail-gateway.md).

## Mailboxes

Each mailbox consists of the following:

 - A directory under the email root, and within this directory
 - Email messages are stored on per file, in plain Apple II text files (with CR line endings) named `EMAIL.nn` where `nn` is an integer value
 - A text file called `NEXT.EMAIL`.  This file initially contains the number 1.  It is used when naming the individual `EMAIL.nn` files, and is incremented by one each time.  If messages are added to a mailbox and nothing is ever deleted they will be sequentially numbered `EMAIL.1`, `EMAIL.2`, etc.
 - A binary file called `EMAIL.DB`.  This file contains essential information about each email message in a quickly accessed format.  This allows the user interface to show the email summary without having to open and read each individual email file.  This file is initially empty and a fixed size record is added for each email message.

The easiest way to create additional mailboxes is using the `N)ew` command in `EMAIL.SYSTEM`.

`POP65.SYSTEM` knows how to initialize `INBOX` but the directory must have been created first.

Note that `SPOOL` is not a mailbox, just a directory.  `OUTBOX` is also not a 'proper' mailbox - it has `NEXT.EMAIL` but not `EMAIL.DB`.

If the `EMAIL.DB` file for a mailbox gets corrupted, it will no longer possible to browse the summary and read the messages in `EMAIL.SYSTEM`.  The utility `REBUILD.SYSTEM` can be used to rebuild the `EMAIL.DB` and `NEXT.EMAIL` files for an existing mailbox (see below.)

## Detailed Documentation

 - [Initial Setup and Configuration](README-emailler-setup.md)
 - [Main Emai//er user interface `EMAIL.SYSTEM`](README-email.md)
 - [Setting System Date and Time with `DATE65.SYSTEM`](README-date65.md)
 - [Text Editor `EDIT.SYSTEM`](README-edit.md)
 - [Attaching Files with `ATTACHER.SYSTEM`](README-attacher.md)
 - [Receiving Email with `POP65.SYSTEM`](README-pop65.md)
 - [Sending Email with `SMTP65.SYSTEM`](README-smtp65.md)
 - [Rebuilding Mailboxes with `REBUILD.SYSTEM`](README-rebuild.md)

## Usenet News

### Overview of Usenet News Support

Emai//er supports receiving and sending of Usenet news articles. It has been tested extensively using the Eternal September NNTP server.  Usenet newsgroups are mapped to mailboxes within `EMAIL.SYSTEM`.  A new mailbox is created for each subscribed newsgroup.

### Configuration File `NEWS.CFG`

The news configuration file is called `NEWS.CFG`.  It is a straightforward ProDOS text file, with one parameter per line.  You may edit this file using the provided editor, `EDIT.SYSTEM` (or any other ProDOS text editor).  When editing the file be careful not to add or delete any lines - this file has no grammar and the lines *must* appear in the expected order.

To edit the file using `EDIT.SYSTEM`:

  - Run `EDIT.SYSTEM` using Bitsy Bye or your usual ProDOS launcher.
  - Press Open Apple-O to open a file, then enter `NEWS.CFG` at the prompt, followed by return.
  - Editing is fairly intuitive.  Use the arrow keys to move around and type to insert text.  Open Apple-Delete deletes to the right.
  - When you are satisfied, save the file using Open Apple-S.
  - Quit the editor using Open Apple-Q.

All three of the programs that handle news: `EMAIL.SYSTEM`, `NNTP65.SYSTEM` and `NNTP65UP.SYSTEM` share this configuration file.

Here is an example config file (with passwords replaced with `****` for obvious reasons):

```
144.76.35.198:119
Bobbi
****
/H1/IP65
/DATA/EMAIL
bobbi.8bit@gmail.com
```

The lines are as follows, in order:

 1) IP address of the NNTP server, optionally followed by a colon and then the TCP port number.  If the colon and port number are omitted, port 119 is the default.
 2) Username to use when connecting to NNTP.
 3) Password to use when connecting to NNTP.
 4) ProDOS path of the directory where the email executables are installed.
 5) ProDOS path to the root of the email folder tree.  Mailboxes will be created and managed under this root path.
 6) Your email address.  Used as the sender's address in outgoing messages.

### Configuration file `NEWSGROUPS.CFG`

This configuration file is found in the email root directory (`/H1/DOCUMENTS/EMAIL` in our example).  This file records the list of Usenet news groups to which emai//er is subscribed.  `NNTP65.SYSTEM` will automatically update this file each time news articles are downloaded from the server in order to record the most recent article received.

An example may look as follows:

```
comp.sys.apple2 CSA2 60260
comp.sys.apple2.programmer CSA2P 7740
comp.emulators.apple2 CEA2 4300
comp.os.cpm COC 15964
```

Each line contains the following three fields, separated by a space:

1) Name of newsgroup
2) Name of Emai//er mailbox which will be used for this newsgroup
3) Most recent message number downloaded

### Creating Directories

A number of additional subdirectories are required within the email root directory for handling Usenet news articles.  The email root directory is assumed to be `/H1/DOCUMENTS/EMAIL` in this example.  Special news directories are as follows:

 - The `NEWS.SPOOL` directory is used by `NNTP65.SYSTEM` as a staging area for incoming news articles before they are copied to the mailbox which is configured for the newsgroup in question.  This will be `/H1/DOCUMENTS/EMAIL/NEWS.SPOOL` in our example.
 - The `NEWS.OUTBOX` directory is used by `EMAIL.SYSTEM` for composing outgoing news articles. `NNTP65UP.SYSTEM` takes outgoing articles from this directory.  In our example this will be `/H1/DOCUMENTS/EMAIL/NEWS.OUTBOX`.

You can create these directories in ProDOS `BASIC.SYSTEM` as follows:

```
] CREATE /H1/DOCUMENTS/EMAIL/NEWS.SPOOL
] CREATE /H1/DOCUMENTS/EMAIL/NEWS.OUTBOX
```

### Creating Mailboxes

You must set up a `NEWS.SENT` mailbox, otherwise `NNTP65UP.SYSTEM` will be unable to complete the sending of messages and will give an error.  You will also need to create a mailbox for each newsgroup you wish to subscribe to.  The name of the newsgroup mailboxes must match that given in `NEWSGROUPS.CFG` or `NNTP65.SYSTEM` will give an error when downloading news articles.

To create these mailboxes, run `EMAIL.SYSTEM` and press `N` for N)ew mailbox.  At the prompt, enter the name of the mailbox to be created: `RECEIVED`, and press return.  Repeat this to create the `SENT` mailbox.

### Subscribing to a Newsgroup

Suppose you want to subscribe to newsgroup `comp.sys.pdp11`.

1) Add a new line to `/H1/DOCUMENTS/EMAIL/NEWSGROUPS.CFG` as follows: `alt.sys.pdp11 ASP11 0`
2) Run `EMAIL.SYSTEM` and using `N)ew` command to create mailbox `ASP11`, matching the line in `NEWSGROUPS.CFG`.  You may use any name you choose.
3) Use the Closed Apple-R command to run `NNTP65.SYSTEM` and retreive messages from the newly-subscribed newsgroup.

When the 'last message' field of the newgroup is zero, `NNTP65.SYSTEM` will download the most recent 100 articles from the newsgroup.  It will then set the most recent article counter in `NEWSGROUPS.CFG` so that subsequent runs will retrieve new messages only.

## `NNTP65.SYSTEM`

*Run using Closed Apple-R in `EMAIL.SYSTEM`*

...

## `NNTP65UP.SYSTEM`

*Run using Closed Apple-S in `EMAIL.SYSTEM`*

...

