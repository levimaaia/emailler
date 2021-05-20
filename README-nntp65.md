# Apple II Email and Usenet News Suite

<p align="center"><img src="img/emailler-logo.png" alt="emai//er-logo" height="200px"></p>

[Back to Main emai//er Docs](README.md#detailed-documentation-for-usenet-functions)

## `NNTP65.SYSTEM`

*Run using `Closed Apple`-`R` in `EMAIL.SYSTEM`*

<p align="center"><img src="img/NNTP65.jpg" alt="NNTP65" height="400px"></p>

`NNTP65.SYSTEM` is a Network News Transport Protocol (NNTP) client for the Apple II. It requires an Uthernet-II ethernet card and will not work with other interfaces without modification, because it uses the W5100 hardware TCP/IP stack.

`NNTP65.SYSTEM` handles downloading of news articles. Transmission is handled by [`NNTP65UP.SYSTEM`](README-nntp65up.md).

Before running `NNTP65.SYSTEM` for the first time, be sure you have performed the basic Emai//er setup described [here](README-emailler-setup.md) and have also performed the initialization specific to Usenet discussed in [this page](README-usenet-setup.md).  In particular, the `NEWS.SPOOL` directory must have been created, otherwise `NNTP65.SYSTEM` will be unable to download news articles.

`NNTP65.SYSTEM` runs without any user interaction and performs the following tasks:

 - Detect Uthernet-II.
 - Obtain IP address using DHCP.
 - If there is a file named `KILL.LIST.CFG` then load the file into memory. Each line is treated as a separate kill pattern. See the subsection [Kill File](#Kill-File) below.
 - Connect to NNTP server using parameters from first three lines of `NEWS.CFG`. (`AUTHINFO USER` and `AUTHINFO PASS` commands).
 - For each newsgroup listed in `NEWSGROUPS.CFG`:
   - Issue `GROUP` command to select the newsgroup.
   - Parse the respond from the server which indicates the message number of the first and last messages available in the newsgroup. 
   - If the article number in `NEWSGROUPS.CFG` is zero, set the current article number to the last available article number minus 100 (so that up to 100 articles are retrieved when the newsgroup is retrieved for the first time.) Otherwise, set the current article number to the first article number recorded in `NEWSGROUPS.CFG`.
   - Issue the `STAT` command to the NNTP server for the current article number.
   - If the article does not exist, increment the current article number and loop to the previous step.
   - For each article to be retrieved from the selected newsgroup:
     - Issue the `NEXT` command to the NNTP server to advance to the next article.
     - Issue to `ARTICLE` command to retrieve the news article, writing it to a file in the `NEWS.SPOOL` directory (eg: `/H1/DOCUMENTS/EMAIL/NEWS.SPOOL/NEWS.1234`).
   - Once all articles have been retrieved for this newsgroup, write an updated newsgroup line to the file `NEWSGROUPS.NEW`. This will be identical to the line read from `NEWSGROUPS.CFG` except with the last article number updated.
   - Examine each message in `NEWS.SPOOL`. If there is a kill-file, the 'From:' header of the message is compared to each line in the killfile and if the text in the kill-file matches any part of the 'From:' header, the message is discarded.  If there is no match, the message is copied from the `NEWS.SPOOL` directory to the mailbox for the newsgroup in question.  If there is no kill-file then all messages are copied unconditionally.
 - Once all newsgroups have been retrieved, rename `NEWSGROUPS.NEW` to replace `NEWSGROUPS.CFG`.
 - Issue the `QUIT` command to disconnect from the NNTP server.
 - If `NNTP65.SYSTEM` was invoked from `EMAIL.SYSTEM`, load and run `EMAIL.SYSTEM`. Otherwise quit t
o ProDOS.

### Kill File

It is possible to define a kill file which is used to filter incoming news articles according to the value of the 'From:' header. This facility is useful to filter out spam from a small number of nuisance senders.

 - The kill file is a plain Apple II text file named `KILL.LIST.CFG`. You can create and maintain such a file using Emai//er's `EDIT.SYSTEM` or any other Apple II text editor.
 - Each line in the kill file is treated as a separate pattern.
 - Pattern matching is very simple. `NNTP65.SYSTEM` simply checks each 'From:' header against each line in the kill file in turn. If any substring of the 'From:' header matches any line of the kill file, that message is discarded.
 - Only the first approximately 80 characters of each 'From:' header are examined.
 - Subject to the limitation above, the matching can apply to any text in the 'From:' header, full names or email address.
 - Don't make kill files too big. It will slow down news processing and (eventually) exhaust all the precious memory in your Apple II!

[Back to Main emai//er Docs](README.md#detailed-documentation-for-usenet-functions)

