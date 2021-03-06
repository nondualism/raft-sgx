/* App.c
*
* Copyright (C) 2006-2016 wolfSSL Inc.
*
* This file is part of wolfSSL.
*
* wolfSSL is free software; you can redistribute it and/or modify
* it under the terms of the GNU General Public License as published by
* the Free Software Foundation; either version 2 of the License, or
* (at your option) any later version.
*
* wolfSSL is distributed in the hope that it will be useful,
* but WITHOUT ANY WARRANTY; without even the implied warranty of
* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
* GNU General Public License for more details.
*
* You should have received a copy of the GNU General Public License
* along with this program; if not, write to the Free Software
* Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1335, USA
*/


#include "stdafx.h"
#include "App.h" /* contains include of Enclave_u.h which has wolfSSL header files */
#include "logging.h"

/* Use Debug SGX ? */
#if _DEBUG
	#define DEBUG_VALUE SGX_DEBUG_FLAG
#else
	#define DEBUG_VALUE 1
#endif

/* the usual suspects */
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>

/* socket includes */
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <unistd.h>

#include <sgx_uae_service.h>
#include <sgx_ukey_exchange.h>

#include <libdill.h>

#include <wolfssl/certs_test.h>

#define Raft_protocol (0)
#define Https_protocol (1)

int counter =0;

typedef struct tcp_conn_s{
  char ip[46];
  int port;
  int tcp_connected;
  int socket;
  int peer_is_client;
  void* tls_conn;
  struct tcp_conn_s* next;
  char* id;
} tcp_conn_t;

tcp_conn_t* tcp_conns;

coroutine void conn_recv(sgx_enclave_id_t eid, tcp_conn_t* tcp_conn){
  int sgx_status, ret;
  int s = tcp_conn->socket;
  void* tls_conn = tcp_conn->tls_conn;
  unsigned char* buf_recv = malloc(4096*sizeof(unsigned char));
  memset(buf_recv, '\0', 4096);
  int len_tcp_to_recv = 5;
  int buf_recv_len = 0;
  Debug("id:%s, try to recv %d bytes\n", tcp_conn->id, len_tcp_to_recv);
  // recv tcp data, and feed to the associated enclave.
  clock_t total_start = clock();
  clock_t io_total = 0;
  while(1){
    clock_t io_start = clock();
    ret =  brecv(s, buf_recv, len_tcp_to_recv, -1);
    clock_t io_end = clock();
    io_total += (io_end - io_start);
    if(ret < 0){
      Error("id:%s, brecv error: %d\n",tcp_conn->id, errno);
      break;
    }
    Error("len_tcp_recved:%d,ret:%d\n",len_tcp_to_recv,ret);
    buf_recv_len = len_tcp_to_recv;
    Debug("id:%s, buf_recv_len: %d\n", tcp_conn->id, buf_recv_len);
    sgx_status = ecall_push_tcp(eid, &len_tcp_to_recv, tls_conn, buf_recv, buf_recv_len);
    Error("after ecall, len_tcp_to_recv:%d,ret:%d\n",len_tcp_to_recv,ret);
    buf_recv_len = len_tcp_to_recv;
    if(sgx_status !=0){
      Error("id:%s, ecall_push_tcp_of_raft failed\n", tcp_conn->id);
      break;
    }

    // try send
    int len_to_flush = 0;
    unsigned char* buf_send = malloc(4096*sizeof(unsigned char));
    memset(buf_send, '\0', 4096);
    sgx_status = ecall_pull_tcp(eid, &len_to_flush, tls_conn, buf_send, 4096);
    if(sgx_status !=0){
      Error("ecall_pull_tcp_of_raft failed, errno:%d\n", sgx_status);
      break;
    }else if(len_to_flush > 0){
      Debug("%s, len to send:%d\n", tcp_conn->id, len_to_flush);
      //      Debug("to send:%s\n", buf_send);
      ret = bsend(s, buf_send, len_to_flush, -1);
      if(ret<0){
        Error("%s, bsend %d bytes ret: %d %d\n", tcp_conn->id, len_to_flush, ret, errno);
        break;
      }else{
        Debug("%s, send %d butes succeeded\n", tcp_conn->id, len_to_flush);
      }
    }

    // end try send


    if(len_tcp_to_recv ==0){
      //to-do: clean up this tcp_conn and close
      tcp_conn->tcp_connected = 0;
      break;
    }
  }
  clock_t total_end = clock();
  clock_t total = total_end - total_start;
  Error("io_total:%f\n", (double)io_total);
  Error("total:%f\n", (double)total);
  Error(" io / total = %f\n", (double)(io_total/total) );
  Error("clocks_per_second: %ld\n", CLOCKS_PER_SEC);
  tcp_conn->tcp_connected = 0; //wanggang add.
  free(buf_recv);  //wanggang add.
  Error("id %s exit\n", tcp_conn->id);

  /** merge send */
  int rc = tcp_close(s, -1);
  if(rc != 0) {
    Error("tcp close failed! errno:%d\n", errno);
  }else{
    Error("id:%s, tcp close succeeded\n", tcp_conn->id);
  }
  /** merge send */
}



coroutine void conn_send(sgx_enclave_id_t eid, tcp_conn_t* tcp_conn){
  int sgx_status, ret;
  int len_to_flush = 0;
  int s = tcp_conn->socket;
  void* tls_conn = tcp_conn->tls_conn;
  unsigned char* buf_send = malloc(4096*sizeof(unsigned char));
  memset(buf_send, '\0', 4096);
  while(1){
    sgx_status = ecall_pull_tcp(eid, &len_to_flush, tls_conn, buf_send, 4096);
    if(sgx_status !=0){
      Error("ecall_pull_tcp_of_raft failed, errno:%d\n", sgx_status);
      break;
    }else if(len_to_flush > 0){
      Debug("%s, len to send:%d\n", tcp_conn->id, len_to_flush);
      //      Debug("to send:%s\n", buf_send);
      ret = bsend(s, buf_send, len_to_flush, -1);
      if(ret<0){
        Error("%s, bsend %d bytes ret: %d %d\n", tcp_conn->id, len_to_flush, ret, errno);
        break;
      }else{
        Debug("%s, send %d butes succeeded\n", tcp_conn->id, len_to_flush);
      }
    }else{

      //      continue;
    }
    Debug("new peer tcp_connected: %d\n", tcp_conn->tcp_connected);
    if(!tcp_conn->tcp_connected){
      int rc = tcp_close(s, -1);
      if(rc != 0) {
        Error("tcp close failed! errno:%d\n", errno);
      }else{
        Error("id:%s, tcp close succeeded\n", tcp_conn->id);
      }
      break;
    }
    int rc = msleep(now() + 1);
    if(rc != 0) {
      Error("cannot sleep!");
    }
    //    yield();
  }
  Error("connection %s exit\n", tcp_conn->id);
  free(tcp_conn);
  free(buf_send);
}

int conn_new(sgx_enclave_id_t eid, WOLFSSL_CTX* ctx, tcp_conn_t* tcp_conn, int protocol) {
  int sgx_status;
  void* tls_conn;

  sgx_status = ecall_conn_new(eid, &tls_conn, ctx,
                              tcp_conn->ip, strlen(tcp_conn->ip)+1,
                              tcp_conn->socket, tcp_conn->peer_is_client, protocol,
                              tcp_conn->id, strlen(tcp_conn->id)+1);
  if(sgx_status !=0){
    Error("id %s, ecall_tcp_conn_new failed\n", tcp_conn->id);
  }
  tcp_conn->tls_conn = tls_conn;
  return 0;
}

tcp_conn_t* find_peer(char* ip){
  tcp_conn_t* tcp_conn = tcp_conns;
  while(tcp_conn){
    if(strcmp(tcp_conn->ip, ip)==0){
      return tcp_conn;
    }
    tcp_conn = tcp_conn->next;
  }
  return NULL;
}

coroutine void start_raft_server(sgx_enclave_id_t eid, WOLFSSL_CTX* ctx, char* server_ip){
  Warn("starting raft server, ip:%s\n", server_ip);
  struct ipaddr addr;
  int rc = ipaddr_local(&addr, server_ip, 5555, 0);
  if (rc < 0) {
    Error("Can't open listening socket\n");
    return;
  }
  // tcp_listen(addr, baklog), backlog: maximum number of connections kept open
  int ls = tcp_listen(&addr, 10);
  assert(ls >= 0 );

  while(1) {
    struct ipaddr c_addr;
    int s = tcp_accept(ls, &c_addr, -1);
    assert(s >= 0);

    char* client_ip = malloc(IPADDR_MAXSTRLEN);
    ipaddr_str(&c_addr, client_ip);
    //    int port = ipaddr_port(&c_addr);

    time_t now;
    now = time(NULL);
    char* id;
    asprintf(&id, "%s@%ld", client_ip, now);

    tcp_conn_t* tcp_conn = find_peer(client_ip);
    if(tcp_conn){
      if(tcp_conn->tcp_connected ==1){
        //to-do: more cleaning up
        tcp_close(s, -1);
      }else{
        Debug("one peer connected\n");
        tcp_conn->id = id;
        tcp_conn->tcp_connected = 1;
        tcp_conn->peer_is_client = 1;
        tcp_conn->socket = s;
        int rc = conn_new(eid, ctx, tcp_conn, Raft_protocol);
        if(rc!=0){
          Error("cannot sleep");
          exit(1);
        }
        rc = go(conn_recv(eid, tcp_conn));
        assert(rc>=0);
        rc = go(conn_send(eid, tcp_conn));
        assert(rc>=0);
        Warn("id:%s, raft peer(client) %s connected\n", tcp_conn->id, tcp_conn->ip);
      }
    }else{
      Error("unauthorized conn\n");
      tcp_close(s, -1);
    }
  }

}

coroutine void start_https_server(sgx_enclave_id_t eid, WOLFSSL_CTX* ctx, char* server_ip){
  Debug("start https server\n");
  struct ipaddr addr;

  int rc = ipaddr_local(&addr, server_ip, 6666, 0);
  if (rc < 0) {
    perror("Can't open listening socket");
    return;
  }
  int ls = tcp_listen(&addr, 10);
  assert( ls >= 0 );

  while(1) {
    struct ipaddr c_addr;
    int s = tcp_accept(ls, &c_addr, -1);
    if(s==-1){
      Error("tcp_accept failed, errno: %d", errno);
    }
    counter++;

    tcp_conn_t* tcp_conn = malloc(sizeof(tcp_conn_t));
    tcp_conn->tcp_connected =1;
    tcp_conn->socket = s;
    tcp_conn->peer_is_client = 1;
    ipaddr_str(&c_addr, tcp_conn->ip);
    //    int port = ipaddr_port(&c_addr);

    time_t now;
    now = time(NULL);
    //    asprintf(&tcp_conn->id, "%s@%ld", tcp_conn->ip, now);
    asprintf(&tcp_conn->id, "%s@%d", tcp_conn->ip, counter);
    Error("id:%s, https client %s connected\n", tcp_conn->id, tcp_conn->ip);

    rc = conn_new(eid, ctx, tcp_conn, Https_protocol);
    assert(rc >= 0);
    //    rc = go(conn_recv(eid, tcp_conn));
    //    assert(rc>=0);
    conn_recv(eid,tcp_conn);
    //    rc = go(conn_send(eid, tcp_conn));
    //    assert(rc>=0);
  }
}

coroutine void start_raft_client(sgx_enclave_id_t eid, WOLFSSL_CTX* ctx, tcp_conn_t* tcp_conn){
  struct ipaddr peer_addr;
  ipaddr_local(&peer_addr, tcp_conn->ip, tcp_conn->port, 0);
  // deadline must be -1;
  //Warn("try cannect raft peer(server), ip:%s\n", tcp_conn->ip);
  int s = tcp_connect(&peer_addr, 0);
  if(s>0){
    if(tcp_conn->tcp_connected != 1){
      tcp_conn->tcp_connected = 1;
      tcp_conn->socket = s;
      tcp_conn->peer_is_client = 0;
      time_t now;
      now = time(NULL);
      asprintf(&tcp_conn->id, "%s@%ld", tcp_conn->ip, now);
      Warn("ConnectionId:%s, raft peer(server) %s connected\n", tcp_conn->id, tcp_conn->ip);
      int rc = conn_new(eid, ctx, tcp_conn, Raft_protocol);
      assert(rc>=0);
      rc = go(conn_recv(eid, tcp_conn));
      assert(rc>=0);
      rc = go(conn_send(eid, tcp_conn));
      assert(rc>=0);
    }else{
      tcp_close(s, -1);
    }
  }else{
   // Error("tcp connect failed\n");
  }
}

coroutine void start_raft_clients(sgx_enclave_id_t eid, WOLFSSL_CTX* ctx){
  while(1){
    tcp_conn_t* tcp_conn = tcp_conns;
    while(tcp_conn){
      if(tcp_conn->tcp_connected != 1){
        start_raft_client(eid, ctx, tcp_conn);
      }
      tcp_conn = tcp_conn->next;
    }
    msleep(now()+10);
  }
}

coroutine void raft_loop(sgx_enclave_id_t eid){
  while(1){
    msleep(now()+1000);
    int ret=0;
    ecall_raft_periodic(eid, &ret);
    Debug("raft periodic check done, sleep 1 s\n");
  }
}



int main(int argc, char* argv[]) /* not using since just testing w/ wc_test */
{
  sgx_enclave_id_t id;
  sgx_launch_token_t t;

  int ret = 0;
  int sgx_status = 0;
  int updated = 0;

  memset(t, 0, sizeof(sgx_launch_token_t));

  sgx_status = sgx_create_enclave(ENCLAVE_FILENAME, DEBUG_VALUE, &t, &updated, &id, NULL);
  if (sgx_status != SGX_SUCCESS) {
    Error("Failed to create Enclave : error %d - %#x.\n", ret, ret);
    return 1;
  }

  //  cache_verify_buffer(id);
  WOLFSSL_CTX* ctx;
  sgx_status = ecall_setup_wolfssl_ctx(id, &ctx);

  // parse cluster config, create tcp_conns and copy into enclaves
  // start raft_server
  // start other raft_clients

  if(argc != 4){
    Debug("wrong args\n");
    return -1;
  }

  char* server_ip = argv[1];

  Warn("starting raftee node... ip:%s\n\n\n", server_ip);
  tcp_conn_t* peer1 = malloc(sizeof(tcp_conn_t));
  //  peer1->ip = argv[2];
  strcpy(peer1->ip, argv[2]);
  Debug("peer 1 ip: %s\n", peer1->ip);
  peer1->port = 5555;
  peer1->tcp_connected = 0;
  peer1->socket = -1;
  peer1->peer_is_client = 0;
  peer1->tls_conn = NULL;

  tcp_conn_t* peer2 = malloc(sizeof(tcp_conn_t));
  //  peer2->ip = argv[3];
  strcpy(peer2->ip, argv[3]);
  peer2->port = 5555;
  peer2->tcp_connected = 0;
  peer2->socket = -1;
  peer2->peer_is_client = 0;
  peer2->tls_conn = NULL;

  peer1->next = peer2;
  peer2->next = NULL;
  tcp_conns = peer1;

  sgx_status = ecall_raft_setup(id, &ret);
  Debug("ecall raft setup done\n");
  //  if(strcmp(server_ip, "192.168.2.101")==0){
  //    sgx_status = ecall_raft_become_leader(id, &ret);
  //  }
 // go(start_raft_server(id, ctx, server_ip));
//  go(start_raft_clients(id, ctx));
//  go(raft_loop(id));
  start_https_server(id, ctx, server_ip);
  return 0;
}

void ocall_print_string(const char *str)
{
    /* Proxy/Bridge will check the length and null-terminate 
     * the input string to prevent buffer overflow. 
     */
  printf("%s", str);
}

time_t ocall_time(time_t* timer){
  return time(timer);
}

void ocall_low_res_time(int* time)
{
    struct timeval tv;
    gettimeofday (&tv, NULL);
    if(!time) return;
    *time = tv.tv_sec;
    return;
}
