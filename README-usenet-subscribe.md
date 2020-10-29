# Apple II Email and Usenet News Suite

<p align="center"><img src="img/emailler-logo.png" alt="emai//er-logo" height="200px"></p>

[Back to Main emai//er Docs](README-emailler.md#detailed-documentation-for-usenet-functions)

## Subscribing to a Newsgroup

Suppose you want to subscribe to newsgroup `comp.sys.pdp11`.

1) Add a new line to `/H1/DOCUMENTS/EMAIL/NEWSGROUPS.CFG` as follows: `alt.sys.pdp11 ASP11 0`
2) Run `EMAIL.SYSTEM` and using `N)ew` command to create mailbox `ASP11`, matching the line in `NEWSGROUPS.CFG`.  You may use any name you choose.
3) Use the `Closed Apple`-`R` command to run `NNTP65.SYSTEM` and retreive messages from the newly-subscribed newsgroup.

When the 'last message' field of the newgroup is zero, `NNTP65.SYSTEM` will download the most recent 100 articles from the newsgroup.  It will then set the most recent article counter in `NEWSGROUPS.CFG` so that subsequent runs will retrieve new messages only.

[Back to Main emai//er Docs](README-emailler.md#detailed-documentation-for-usenet-functions)

