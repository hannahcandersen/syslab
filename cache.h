/**
 * @author Hannah Andersen <hcan@itu.dk>
 */

#include <pthread.h>

#define MAX_CACHE_SIZE 1049000
#define MAX_OBJECT_SIZE 102400
// The maximum data ever used for web objects will be MAX_CACHE_SIZE + T * MAX_OBJECT_SIZE

/************** Data structure declarations ****************/
/**
 * @brief Queue implemented using double linked list that supports both LIFO and FIFO operations *
 */

typedef struct CacheEntry
{
    char *request;            // Request sent by the client
    char *response;           // Response sent by the server
    size_t entry_size;        // Size of the entry
    struct CacheEntry *next;  // Next entry in the cache
    struct CacheEntry *prev;  // Previous entry in the cache
} CacheEntry;

typedef struct CacheQueue
{
    CacheEntry *head;         // First element
    CacheEntry *tail;         // Last element
    size_t cache_size;        // Current size of the cache
} CacheQueue;

/************** Operations on queue ************************/

CacheQueue* initializeCache();

char *findRequestInCache(char *clientRequest, CacheQueue *cache);

void promoteEntry(CacheEntry *entry, CacheQueue *cache);

CacheEntry *createNewEntry(char *clientRequest, char *serverResponse, CacheQueue *cache);

void putEntryToFront(CacheEntry *entry, CacheQueue *cache);

void evictEntryFromEnd(CacheEntry *oldEntry, CacheQueue *cache);

void cleanupCache(CacheQueue *cache);
