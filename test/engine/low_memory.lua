#!/usr/bin/env tarantool
os = require('os')
box.cfg({
    listen              = os.getenv("LISTEN"),
    memtx_memory        = 32 * 1024 * 1024,
})

require('console').listen(os.getenv('ADMIN'))
