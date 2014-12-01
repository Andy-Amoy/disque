/*
 * Copyright (c) 2014, Salvatore Sanfilippo <antirez at gmail dot com>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 *   * Redistributions of source code must retain the above copyright notice,
 *     this list of conditions and the following disclaimer.
 *   * Redistributions in binary form must reproduce the above copyright
 *     notice, this list of conditions and the following disclaimer in the
 *     documentation and/or other materials provided with the distribution.
 *   * Neither the name of Disque nor the names of its contributors may be used
 *     to endorse or promote products derived from this software without
 *     specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */


#ifndef __DISQUE_JOBS_H
#define __DISQUE_JOBS_H

/* A Job ID is 48 bytes:
 * "DISQ" + 40 bytes random hex (160 bits) + 4 bytes TTL.
 * The TTL is encoded inside the Job ID so that nodes without info about
 * the Job can correctly expire ACKs if they can't be garbage collected. */
#define JOB_ID_LEN 48

/* This represent a Job across the system.
 *
 * The Job ID is the unique identifier of the job, both in the client
 * protocol and in the cluster messages between nodes.
 *
 * Times:
 *
 * When the expire time is reached, the job can be destroied even if it
 * was not successfully processed. The requeue time is the amount of time
 * that should elapse for this job to be queued again (put into an active
 * queue), if it was not yet processed. The queue time is the unix time at
 * which the job was queued last time.
 *
 * Note that nodes receiving the job from other nodes via REPLJOB messages
 * set their local time as ctime and etime (they recompute the expire time
 * doing etime-ctime in the received fields).
 *
 * List of nodes and ACKs garbage collection:
 *
 * We keep a list of nodes that *may* have the message (nodes_delivered hash),
 * so that the creating node is able to gargage collect ACKs even if not all
 * the nodes in the cluster are reachable, but only the nodes that may have a
 * copy of this job. The list includes nodes that we send the message to but
 * never received the confirmation, this is why we can have more listed nodes
 * than the 'repl' count.
 *
 * This optimized GC is possible when a client ACKs the message or when we
 * receive a SETACK message from another node. Nodes having just the
 * ACK but not a copy of the job instead, need to go for the usual path for
 * ACKs GC, that need a confirmation from all the nodes.
 *
 * Body:
 *
 * The body can be anything, including the empty string. Disque is
 * totally content-agnostic. When the 'body' filed is set to NULL, the
 * job structure just represents an ACK without other jobs information.
 * Jobs that are actually just ACKs are created when a client sends a
 * node an ACK about an unknown Job ID, or when a SETACK message is received
 * about an unknown node. */

#define JOB_STATE_WAIT_REPL 0  /* Waiting to be replicated enough times. */
#define JOB_STATE_ACTIVE    1  /* Not acked, not queued, still active job. */
#define JOB_STATE_QUEUED    2  /* Not acked, but queued in this node. */
#define JOB_STATE_ACKED     3  /* Acked, no longer active, to garbage collect.*/

#define JOB_FLAG_BCAST_QUEUED (1<<0) /* Broadcast msg when re-queued. */

#define JOB_GC_RETRY_PERIOD (60*3) /* Try to GC again every 3 minutes. */

/* Job representation in memory. */
typedef struct job {
    char id[JOB_ID_LEN];    /* Job ID. */
    uint8_t state;          /* Job state: one of JOB_STATE_* states. */
    uint8_t flags;          /* Job flags. */
    uint16_t repl;          /* Replication factor. */
    uint32_t etime;         /* Job expire time. */
    uint64_t ctime;         /* Job creation time, local node at creation.
                               ctime is time in milliseconds * 1000000, each
                               job created in the same millisecond in the same
                               node gets prev job ctime + 1. */
    uint32_t delay;         /* Delay before to queue this job for 1st time. */
    uint32_t retry;         /* Job re-queue time: re-queue period in seconds. */
    /* Up to this point we use the structure for on-wire serialization,
     * before here all the fields should be naturally aligned, and pointers
     * should only be present after. */
    robj *queue;            /* Job queue name. */
    sds body;               /* Body, or NULL if job is just an ACK. */
    dict *nodes_delivered;  /* Nodes we delievered the job for replication. */
    dict *nodes_confirmed;  /* Nodes that confirmed to have a copy. */
    uint32_t qtime;         /* Next queue time: local unix time the job will be
                               requeued in this node if not ACKed before.
                               Qtime is updated when we receive QUEUED
                               messages to avoid to re-queue if other nodes
                               did. When qtime is set to zero for a job, it
                               never gets re-queued again. */
    uint32_t awakeme;       /* Time at which we need to take actions about this
                               job in this node. All the registerd jobs are
                               ordered by awakeme time in the server.awakeme
                               skip list, unless awakeme is set to zero. */
} job;

/* Number of bytes of directly serializable fields in the job structure. */
#define JOB_STRUCT_SER_LEN (JOB_ID_LEN+1+1+2+4+8+4+4)

void deleteJobFromCluster(job *j);
sds serializeJob(job *j);
void fixForeingJobTimes(job *j);
void updateJobNodes(job *j);
job *deserializeJob(unsigned char *p, size_t len, unsigned char **next);
int registerJob(job *j);
void freeJob(job *j);
void jobReplicationAchieved(job *j);
job *lookupJob(char *id);
void updateJobAwakeTime(job *j, uint32_t at);
void updateJobRequeueTime(job *j, time_t qtime);

#endif
