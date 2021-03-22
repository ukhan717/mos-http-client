# Download URL
Fetches data from http endpoint based on cron job or endpoint specified in an RPC.
## Overview
Makes use of some very interesting features provided by mongoose os, calls http endpoints and writes the response data
to a file.

Cron jobs are triggered periodically and RPCs could be called at any point in time to perform the http fetch.

By using `cron` format for time specification, the program logs the outcome of each request performed in a file. 


To trigger call to http endpoint issue this command
`mos call http_fetch_handler ‘{“url”: “http://mongoose-os.com/downloads/mos/version.json”, “file”: “x.json”}`

Code is tested on ESP8266
