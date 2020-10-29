# Apple II Email and Usenet News Suite

<p align="center"><img src="img/emailler-logo.png" alt="emai//er-logo" height="200px"></p>

[Back to Main emai//er Docs](README-emailler.md)

## `REBUILD.SYSTEM`

REBUILD is a utility for converting a folder of email messages (text files named `EMAIL.nnn` where `nnn` is an integer) into a mailbox.  It will erase any existing `EMAIL.DB` and `NEXT.EMAIL` files, parse the message files and create new `EMAIL.DB` and `NEXT.EMAIL` files.  This tool may be used for bulk import of messages or for recreating the `EMAIL.DB` file for a mailbox which has become corrupted.

REBUILD simply prompts for the path of the directory to process.

If you use this tool for bulk import, be sure that all the `EMAIL.nnn` files are in Apple II text format with carriage return line endings (not MS-DOS or UNIX style.)


[Back to Main emai//er Docs](README-emailler.md)

