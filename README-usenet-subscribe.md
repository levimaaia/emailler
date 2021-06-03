# Apple II Email and Usenet News Suite

<p align="center"><img src="img/emailler-logo.png" alt="emai//er-logo" height="200px"></p>

[Back to Main emai//er Docs](README.md#detailed-documentation-for-usenet-functions)

## Subscribing to a Newsgroup

This section outlines the steps for subscribing to a newsgroup.

For example, suppose you want to subscribe to newsgroup `comp.sys.pdp11`.

1) The first step is to add a new line to `NEWSGROUPS.CFG` for the newgroup subscription. `NEWSGROUPS.CFG` is found in the email root directory (`/H1/DOCUMENTS/EMAIL` using the example settings.)  Start `EDIT.SYSTEM` and use the `Open Apple`-`O` command to open the file `/H1/DOCUMENTS/EMAIL/NEWSGROUPS.CFG`.
2) Using `EDIT.SYSTEM` add a line to the file consisting of the newsgroup name, a space, the name of the mailbox you want to use to store messages. For example, we may decide to use mailbox name `ASP11` for this newsgroup:

```
alt.sys.pdp11 ASP11
```

3) Save the file with `Open Apple`-`S` and quit `EDIT.SYSTEM` with `Open Apple`-`Q`.
4) Run `EMAIL.SYSTEM` and use the `N` (new mailbox) command to create the mailbox your chose in step 2 above (`ASP11` in this case.)
3) Use the `Closed Apple`-`R` command to run `NNTP65.SYSTEM` and retreive messages from the newly-subscribed newsgroup.

When the 'last message' field of the newgroup is zero, `NNTP65.SYSTEM` will download the most recent 100 articles from the newsgroup.  It will then set the most recent article counter in `NEWSGROUPS.CFG` so that subsequent runs will retrieve new messages only.

[Back to Main emai//er Docs](README.md#detailed-documentation-for-usenet-functions)

