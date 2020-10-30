# Apple II Email and Usenet News Suite

<p align="center"><img src="img/emailler-logo.png" alt="emai//er-logo" height="200px"></p>

[Back to Main emai//er Docs](README.md#detailed-documentation-for-email-functions)

## `SMTP65.SYSTEM`

*Run using `Open Apple`-`S` in `EMAIL.SYSTEM`*

<p align="center"><img src="img/SMTP65.jpg" alt="SMTP65" height="400px"></p>

`SMTP65.SYSTEM` is a Simple Mail Transport Protocol (SMTP65) client for the Apple II.  It requires an Uthernet-II ethernet card and will not work with other interfaces without modification, because it uses the W5100 hardware TCP/IP stack.  `SMTP65.SYSTEM` is used to send outgoing email messages to an SMTP email server.  (I use Postfix on the Raspberry Pi as my SMTP server, but other SMTP servers should work too.)

Before running `SMTP65.SYSTEM` for the first time, be sure to have created the `SENT` mailbox.  This must be a 'proper' mailbox, not just a directory.  You may create a mailbox using the `N` (new mailbox) command in `EMAIL.SYSTEM`.

`SMTP65.SYSTEM` performs the following tasks:

 - Detect Uthernet-II
 - Obtain IP address using DHCP
 - Connect to SMTP server using parameters from lines 5 and 6 of `EMAIL.CFG`. (`HELO` command)
 - Iterate through each message in the `OUTBOX` mailbox (which is `/H1/DOCUMENTS/EMAIL/OUTBOX` with our sample configuration)
   - Scan each message looking for the following headers:
     - `To:`
     - `From:`
     - `cc:`
   - Prompt `S)end message | H)old message in OUTBOX | D)elete message from OUTBOX`
   - If the user chooses `D` then delete the message from `OUTBOX` and contine to the next message (if any).
   - If the user chooses `H` then retain the message in `OUTBOX` and contine to the next message (if any).
   - If the user chooses `S` then proceed to send the message to the SMTP server, as follows:

     - Notify the SMTP server of our email address (from `EMAIL.CFG`). (`MAIL FROM:` command)
     - Notify the SMTP server of each recipient listed in `To:` and `From:` headers (`RCPT TO:` command)
     - Send the email body to the SMTP sender. (`DATA` command)
     - If the message was successfully sent, copy it to the `SENT` mailbox.
     - Remove the sent message from `OUTBOX`.
   - Iterate until all messages in `OUTBOX` have been sent, and copied to `SENT`.  Rejected messages are left in `OUTBOX` where they may be edited and retransmitted.
   - Issue `QUIT` command to SMTP server to disconnect.
 - If `SMTP65.SYSTEM` was invoked from `EMAIL.SYSTEM`, load and run `EMAIL.SYSTEM`. Otherwise quit t
o ProDOS.


[Back to Main emai//er Docs](README.md#detailed-documentation-for-email-functions)

