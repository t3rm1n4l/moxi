moxi
====

ZBase proxy server

## How to build ?

    ./build-rpm
     or
    ./build.sh prefix_path
  
## How to run ?

### Standalone ZBase proxy mode

    $ moxi -X -p 11213
  
  To send commands through moxi, use the following command format:
  
    A:membase_server_ip:port COMMAND\r\n
    or
    B:membase_server_ip:port COMMAND\r\n
  
  A or B prefix signifies whether to use ASCCI or Binary protocol to communicate with ZBase.
  
### ZBase cluster mode
  
    $ moxi -V vbs_server:port
  
  In cluster mode, moxi receives information about the servers in the cluster through vbs cluster manager.
  
  Moxi acts exactly like a ZBase server. Moxi maps keys to vbuckets and it sends commands to server holding that vbucket.
  
  Eg.
  
    echo get key\r\n | nc moxi_server 11213
  
