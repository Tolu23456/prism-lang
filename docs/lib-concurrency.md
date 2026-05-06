# Concurrency Library Documentation

Promises, locks, and asynchronous patterns for Prism.

## Import

```prism
import "lib/concurrency"
```

## Promises

Promise-based async operations with then/catch/finally.

```prism
let promise = Promise(fn(resolve, reject) {
    if condition {
        resolve("success")
    } else {
        reject("error")
    }
})

promise
    .then(fn(value) { print("Success: " + value) })
    .catch(fn(error) { print("Error: " + error) })
    .finally(fn() { print("Done") })
```

## Promise Utilities

```prism
// Wait for all promises
let results = promiseAll([promise1, promise2, promise3])

// Wait for first promise
let first = promiseRace([promise1, promise2])

// Convert callback to promise
let promise = promisify(fn(callback) {
    doAsyncTask(callback)
})
```

## Mutex (Mutual Exclusion Lock)

Ensure only one thread accesses a resource.

```prism
let mutex = Mutex()
let counter = 0

mutex.lock()
counter = counter + 1
mutex.unlock()
```

## Semaphore

Allow N threads to access a resource.

```prism
let semaphore = Semaphore(3)  // Allow 3 concurrent accesses

semaphore.acquire()
// Critical section
semaphore.release()
```

## ReadWriteLock

Multiple readers OR single writer.

```prism
let lock = ReadWriteLock()

// Multiple readers
lock.readLock()
let data = readData()
lock.readUnlock()

// Single writer
lock.writeLock()
writeData(newValue)
lock.writeUnlock()
```

## ThreadPool

Execute tasks in parallel.

```prism
let pool = ThreadPool(4)  // 4 worker threads

for i in range(0, 10) {
    pool.submit(fn() {
        doWork(i)
    })
}

pool.shutdown()
```

## Async Utilities

### Timeout

```prism
timeout(fn() {
    slowOperation()
}, 5000)  // Timeout after 5 seconds
```

### Retry

```prism
retry(fn() {
    unreliableApi()
}, {
    maxAttempts: 3,
    delayMs: 1000,
    backoff: "exponential"  // exponential backoff
})
```

### Series

```prism
series([
    fn() { task1() },
    fn() { task2() },
    fn() { task3() }
])  // Run tasks sequentially
```

### Parallel

```prism
parallel([
    fn() { task1() },
    fn() { task2() },
    fn() { task3() }
])  // Run tasks in parallel
```

## EventEmitter

Pub/Sub messaging pattern.

```prism
let emitter = EventEmitter()

emitter.on("user_created", fn(user) {
    print("New user: " + user.name)
})

emitter.emit("user_created", {id: 1, name: "Alice"})

emitter.off("user_created")  // Unsubscribe
```

## Locks and Synchronization

### Spin Lock

```prism
let lock = SpinLock()
lock.lock()
// Critical section
lock.unlock()
```

### Condition Variable

```prism
let cv = ConditionVariable()
let mutex = Mutex()

// Wait for signal
mutex.lock()
cv.wait(mutex)
mutex.unlock()

// Signal waiting threads
cv.signal()
```

## Async/Await Pattern

```prism
func fetchData() {
    return Promise(fn(resolve, reject) {
        let data = callApi()
        resolve(data)
    })
}

let promise = fetchData()
    .then(fn(data) { processData(data) })
    .then(fn(result) { displayResult(result) })
    .catch(fn(error) { handleError(error) })
```

## Common Patterns

### Concurrent Map

```prism
let urls = ["url1", "url2", "url3"]
let promises = []
for url in urls {
    push(promises, fetchUrl(url))
}
let results = promiseAll(promises)
```

### Producer-Consumer

```prism
let queue = Queue()
let lock = Mutex()

// Producer
func produce(item) {
    lock.lock()
    queue.enqueue(item)
    lock.unlock()
}

// Consumer
func consume() {
    lock.lock()
    let item = queue.dequeue()
    lock.unlock()
    return item
}
```

## Error Handling in Async Code

```prism
promise
    .then(fn(value) {
        if not isValid(value) {
            throw "Invalid value"
        }
        return value
    })
    .catch(fn(error) {
        print("Caught error: " + error)
        return defaultValue
    })
```
