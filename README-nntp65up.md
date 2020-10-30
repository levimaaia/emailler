# Apple II Email and Usenet News Suite

<p align="center"><img src="img/emailler-logo.png" alt="emai//er-logo" height="200px"></p>

[Back to Main emai//er Docs](README.md#detailed-documentation-for-usenet-functions)

## `NNTP65UP.SYSTEM`

*Run using `Closed Apple`-`S` in `EMAIL.SYSTEM`*

<p align="center"><img src="img/NNTP65UP.jpg" alt="SMTP65" height="400px"></p>

`NNTP65UP.SYSTEM` is a Network News Transport Protocol (NNTP) client for the Apple II.  It requires an Uthernet-II ethernet card and will not work with other interfaces without modification, because it uses the W5100 hardware TCP/IP stack.

`NNTP65UP.SYSTEM` handles transmission of news articles to the NNTP server. Reception of news articles is handled by [`NNTP65.SYSTEM`](README-nntp65.md).

Before running `NNTP65UP.SYSTEM` for the first time, be sure to have created the `NEWS.SENT` mailbox.  This must be a 'proper' mailbox, not just a directory.  You may create a mailbox using the `N` (new mailbox) command in `EMAIL.SYSTEM`.

`NNTP65UP.SYSTEM` performs the following tasks:

 - Detect Uthernet-II.
 - Obtain IP address using DHCP.
 - Open the `NEWS.OUTBOX` directory.
 - For each file in `NEWS.OUTBOX`:
   - If file name is `EMAIL.DB` or `NEXT.EMAIL` skip to next.
   - Open the file and search for the headers "Newsgroups:" and "Subject:".
   - Display the newgroup and subject information and prompt the user as follows: `S)end message | H)old message in NEWS.OUTBOX | D)elete message from NEWS.OUTBOX`.
   - If the user chooses `D` then delete the article from `NEWS.OUTBOX` and contine to the next message (if any).
   - If the user chooses `H` then retain the article in `NEWS.OUTBOX` and contine to the next message (if any).
   - If the user chooses `S` then proceed to send the message to the NNTP server, as follows:
     - If not already connected, connect to NNTP server. Check the return code from the server to make sure posting is allowed.
     - Authenticate with the NNTP server using parameters from first three lines of `NEWS.CFG`. (`AUTHINFO USER` and `AUTHINFO PASS` commands)
   - Issue the `POST` command to the NNTP server to start posting an article.
   - Send the contents of the file to the NNTP server.
   - Check the return code from the NNTP server indicates that transmission was successful.
   - Close the file.
   - Copy the article to the `NEWS.SENT` mailbox.
   - Remove the article from the `NEWS.OUTBOX` directory.
 - Issue `QUIT` command to NNTP server to disconnect.
 - If `NNTP65UP.SYSTEM` was invoked from `EMAIL.SYSTEM`, load and run `EMAIL.SYSTEM`. Otherwise quit to ProDOS.

[Back to Main emai//er Docs](README.md#detailed-documentation-for-usenet-functions)

