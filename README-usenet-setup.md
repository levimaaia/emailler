# Apple II Email and Usenet News Suite

<p align="center"><img src="img/emailler-logo.png" alt="emai//er-logo" height="200px"></p>

[Back to Main emai//er Docs](README-emailler.md)

## Setup and Configuration for Usenet

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


[Back to Main emai//er Docs](README-emailler.md)

