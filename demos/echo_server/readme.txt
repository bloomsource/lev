this is a demo of echo server with lev.


-------------------- how to compile -----------------------------
copy lev.h lev.cpp lev_con.h lev_con.cpp dynbuf.h dynbuf.cpp mempool.h mempool.ccp to this dir,
and make


-------------------- how to test--------------------------------------

run echo server:
./echo_server 8000&


test with telent

telnet localhost  8000

type any text, and server will response same text