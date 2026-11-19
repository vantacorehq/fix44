# Security

fix44 does not open a socket. It will not log on to a session and it will not send an order.

The codec rejects non-printable values and refuses a party group larger than 1024 entries. It is still only a parser: do not feed it untrusted bytes and then act on the result without your own checks.

Report a defect as a GitHub issue. Use a private security advisory on this repository if you think the report should not be public. There is no security mailbox and no secret in the tree.
