/**
 * @author Hannah Andersen <hcan@itu.dk>
 */


#include "cache.h" 
#include <stdbool.h>
#include <stdio.h>
#include <pthread.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <netdb.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>

/*
 * Uses a reader-writer lock to ensure that only one thread can write to the cache at a time
 * pthread_rwlock_rdlock: Acquires a read lock. Multiple threads can hold a read lock simultaneously, but if a thread requests a write lock, it will wait until all read locks are released.
 * pthread_rwlock_wrlock: Acquires a write lock. If any thread holds a read or write lock, other threads attempting to acquire a read or write lock will be blocked until the lock is released.
 */
pthread_rwlock_t rwlock;

/* Initialize the cache structure and allocate memory for it */
CacheQueue* initializeCache()
{
    CacheQueue *cache = malloc(sizeof(CacheQueue));
    if (cache == NULL)
    { // Allocation failed
        return NULL;
    }

    cache->head = NULL;    
    cache->tail = NULL; 
    cache->cache_size = 0; 

    // Initialize the read-write lock
    pthread_rwlock_init(&rwlock, NULL);

    printf("\033[33mcache:\033[0m Proxy initialized the cache\n");

    return cache;
}

/* 
* Check if request is in cache 
* If it is, return the response, and promote the entry to the front of the cache
* If it is not, return NULL
*/
char* findRequestInCache(char *clientRequest, CacheQueue *cache)
{
    // Aquire read lock
    pthread_rwlock_rdlock(&rwlock);

    // Iterate through cache entries and compare requests
    CacheEntry *current = cache->head;
    while (current != NULL)
    {
        if (strcmp(current->request, clientRequest) == 0) // Response is found. strcmp returns 0 if strings are equal
        {
            printf("\033[33mcache:\033[0m Proxy found the response in the cache\n");

            // Release read lock
            pthread_rwlock_unlock(&rwlock);

            // To implement LRU, promote the entry to the front of the cache
            promoteEntry(current, cache);

            // Return the response
            return current->response;
        }
        current = current->next;
    }

    // Request not found

    // Release read lock
    pthread_rwlock_unlock(&rwlock);

    printf("\033[33mcache:\033[0m Proxy couldn't find response in cache. Will forward request to server\n");
    return NULL;
}

/*
* To be used on entries already in the cache
* Promote the entry to the front of the cache to ensure LRU
*/
void promoteEntry(CacheEntry *entry, CacheQueue *cache)
{
    // Aquire write lock
    pthread_rwlock_wrlock(&rwlock);

    if (entry->prev == NULL) // Entry is already in the front of the cache
    {
        // Release write lock
        pthread_rwlock_unlock(&rwlock);

        printf("\033[33mcache:\033[0m Requet was already first in the cache\n");
        return;
    }
    else if (entry->next == NULL) // Entry is at the end of the cache
    {
        // Set the second last element as the new tail
        entry->prev->next = NULL;
        cache->tail = entry->prev;

        // Promote the entry to the front of the cache
        entry->next = cache->head; // Set the next reference to the previous head
        cache->head = entry;       // Set the entry as head
        entry->prev = NULL;        // Make sure head has no previous entry
        entry->next->prev = entry; // Set the previous head as second entry

        // Release write lock
        pthread_rwlock_unlock(&rwlock);

        printf("\033[33mcache:\033[0m Proxy promoted a cache entry from end of the cache to the front of the cache\n");

        return;
    }
    else // Entry is in the middle of the cache
    {
        // Link previous and next entries together
        entry->prev->next = entry->next;
        entry->next->prev = entry->prev;

        // Promote the entry to the front of the cache
        entry->next = cache->head; // Set the next reference to the previous head
        cache->head = entry;       // Set the entry as head
        entry->prev = NULL;        // Make sure head has no previous entry
        entry->next->prev = entry; // Set the previous head as second entry

        // Release write lock
        pthread_rwlock_unlock(&rwlock);

        printf("\033[33mcache:\033[0m Proxy promoted a cache entry from the middle of the cache to the front of the cache\n");
        return;
    }
}

/*
 * Create a new cache entry, allocate memory for it and return a pointer to it
 * If allocation fails, return NULL
 * Before calling this function, check that the size of request and response is less than MAX_OBJECT_SIZE
 */
CacheEntry* createNewEntry(char *clientRequest, char *serverResponse, CacheQueue *cache)
{
    CacheEntry *cache_entry;

    // Allocates space for the CacheEntry structure
    cache_entry = malloc(sizeof(CacheEntry));
    if (cache_entry == NULL)
    { // Allocation failed
        printf("\033[33mcache:\033[0m Allocation for cache_entry failed\n");
        return NULL;
    }

    // Calculate the sizes of request and response in bytes
    char *nrequest, *nresponse;
    size_t requestSize = (strlen(clientRequest) + 1) * sizeof(char); 
    size_t responseSize = (strlen(serverResponse) + 1) * sizeof(char); 

    // Allocates space for the request and response 
    nrequest = malloc(requestSize);
    nresponse = malloc(responseSize);
    if (nrequest == NULL)
    { // Allocation failed
        free(cache_entry);
        printf("\033[33mcache:\033[0m Allocation for nrequest failed\n");
        return NULL;
    }
    if (nresponse == NULL)
    { // Allocation failed
        printf("\033[33mcache:\033[0m Allocation for nresponse failed\n");
        free(nrequest);
        free(cache_entry);
        return NULL;    
    }

    // Copy the clientRequest and serverResponse to the entry
    memcpy(nrequest, clientRequest, requestSize);   // (destination, source, number of bytes to copy)
    memcpy(nresponse, serverResponse, responseSize); 

    // Initialize fields of the cache entry
    cache_entry->request = nrequest;
    cache_entry->response = nresponse;
    cache_entry->entry_size = requestSize + responseSize;
    cache_entry->next = NULL;
    cache_entry->prev = NULL;

    printf("\033[33mcache:\033[0m Proxy created a new cache entry with size %zu\n", cache_entry->entry_size);
    // For debugging:
    //printf("\033[33mcache:\033[0m Proxy created a new cache entry with size %zu, request %s and response %s\n",
    //       cache_entry->entry_size, cache_entry->request, cache_entry->response);   

    return cache_entry;
}

/*
* To be used on entries not already in the cache
* Add the entry to the front of the cache
* If there is not enough space in the cache, remove the least used (LRU) entry from the cache and try again
*/
void putEntryToFront(CacheEntry *entry, CacheQueue *cache)
{
    // Aquire write lock
    pthread_rwlock_wrlock(&rwlock);

    if (cache->cache_size + entry->entry_size > MAX_CACHE_SIZE) // No space in cache for new entries
    {
        printf("\033[33mcache:\033[0m There is no space in cache for new entries. Will remove least recently used entry\n");

        // Release write lock
        pthread_rwlock_unlock(&rwlock);

        // Evict the least used cache entry from the cache
        evictEntryFromEnd(entry, cache);
    }
    else // Enough space, add the new entry to the front of the cache
    {
        printf("\033[33mcache:\033[0m There is space in cache for a new entry. Will add it to the front of the cache\n");

        if (cache->head == NULL) // If this is the first inserted entry, it should also be tail
        {
            cache->tail = entry;
            printf("\033[33mcache:\033[0m Congratulations, this is the first cache entry\n");
        }

        // Add the entry to the front of the cache
        entry->next = cache->head; // Set the next reference to the previous head
        cache->head = entry;       // Set the entry as head
        entry->prev = NULL;        // Make sure head has no previous entry
        if (entry->next != NULL)   // Set the previous head as second element
        {
            entry->next->prev = entry;
        }

        // Update the cache size
        cache->cache_size += entry->entry_size;

        // Release write lock
        pthread_rwlock_unlock(&rwlock);
    }
    return;
}

/*
* To be used when there is not enough space in the cache for a new entry.
* Evict the least used (LRU) entry from the cache, which will be the tail of the cache
* After removal, try adding the new entry again
*/
void evictEntryFromEnd(CacheEntry *newEntry, CacheQueue *cache)
{
    // Aquire write lock
    pthread_rwlock_wrlock(&rwlock);

    if (cache->head == cache->tail) // If there is only one entry in the cache, remove it
    {
        // Free the dynamically allocated memory fields of the cache entry
        free(cache->head->request);
        free(cache->head->response); 
        free(cache->head);

        // Set head and tail to NULL
        cache->head = NULL;
        cache->tail = NULL;

        // Update the cache size
        cache->cache_size = 0;

        // Release write lock
        pthread_rwlock_unlock(&rwlock);

        printf("\033[33mcache:\033[0m Proxy removed a cache entry to make space for a more requested one. It was the only entry. Will try to add it again\n");

        // Try adding the entry again
        putEntryToFront(newEntry, cache);

        return;
    }

    // If there are more than one entry in the cache, remove the tail
    CacheEntry *entry = cache->tail;
    
    // Set the second last element as the new tail
    entry->prev->next = NULL; 
    cache->tail = entry->prev;

    // Update the cache size
    cache->cache_size -= entry->entry_size;

    // Free the dynamically allocated memory fields of the cache entry
    free(entry->request);
    free(entry->response);
    free(entry);

    // Release write lock
    pthread_rwlock_unlock(&rwlock);

    printf("\033[33mcache:\033[0m Proxy removed a cache entry to make space for a more requested one. Will try to add it again\n");

    // Try adding the entry again
    putEntryToFront(newEntry, cache);

    return;
}

// Free memory allocated for cache
void cleanupCache(CacheQueue *cache)
{
    // Aquire write lock
    pthread_rwlock_wrlock(&rwlock);

    // Pointer to the cache element we are currently freeing and its next
    CacheEntry *current = cache->head;
    CacheEntry *next;

    // Loop through cache and free all cache entries and their values
    while (current != NULL)
    {
        next = current->next;

        // Free the dynamically allocated memory fields of the cache entry
        free(current->request);
        free(current->response); 
        free(current);

        // Move to next element
        current = next;
    }

    // Release write lock
    pthread_rwlock_unlock(&rwlock);

    // Destroy the read-write lock
    pthread_rwlock_destroy(&rwlock);

    // Free cache structure
    free(cache);

    printf("\033[33mcache:\033[0m Proxy destroyed the cache\n");

    return;
}


