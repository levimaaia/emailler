# Apple II Email and Usenet News Suite

<p align="center"><img src="img/emailler-logo.png" alt="emai//er-logo" height="200px"></p>

[Back to Main emai//er Docs](README-emailler.md#detailed-documentation-for-email-functions)

## `ATTACHER.SYSTEM`

*Automatically invoked when exiting `EDIT.SYSTEM` when using W)rite, F)orward or R)eply in `EMAIL.SYSTEM`*

`ATTACHER.SYSTEM` is used for attaching files to outgoing email messages.  This program is started automatically by `EDIT.SYSTEM` and is not normally invoked directly.

When `EDIT.SYSTEM` invokes `ATTACHER.SYSTEM`, the following operations occur:

 - The outgoing email message is loaded from `OUTBOX` and copied to a temporary file.  While copying, additional email headers are appended to indicate that this is a MIME multi-part message.
 - A `plain/text` MIME section header is added to the email body (which becomes the first section in the multi-part MIME document.)
 - `ATTACHER.SYSTEM` prompts the user to enter the filename of a file to be attached.  This may be an unqualified filename (which will look in the directory the emai//er software is installed in), or a fully-qualified ProDOS pathname, such as `/H1/PATH/TO/MY/FILE.BIN`.
 - For each filename, the file is loaded from disk and encoded using Base64.  A MIME section is created in the outgoing email and the Base64-encoded file data is appended.
 - After all the desired files have been added, enter an empty line to finish adding files.
 - `ATTACHER.SYSTEM` will terminate the MIME document, erase the original email file from `OUTBOX` and rename the temporary file to replace the original.
 - `ATTACHER.SYSTEM` will reload `EMAIL.SYSTEM` once it is done.

[Back to Main emai//er Docs](README-emailler.md#detailed-documentation-for-email-functions)

