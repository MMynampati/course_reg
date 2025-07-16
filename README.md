# ZotReg - Multi-threaded Course Registration Server

A high-performance, thread-safe course registration server written in C that handles concurrent client connections for university course enrollment, waitlisting, and schedule management.

## 🚀 Features

### Core Functionality
- **Multi-threaded Architecture** - One thread per client connection for maximum concurrency
- **Real-time Course Management** - Live enrollment, dropping, and waitlisting
- **User Session Management** - Login/logout with reconnection support
- **Course Listing** - Browse available courses with enrollment status
- **Schedule Viewing** - Personal schedule with enrolled and waitlisted courses
- **Comprehensive Logging** - All server activities logged for auditing

### Client Operations
- **`LOGIN`** - Authenticate and establish session
- **`LOGOUT`** - Graceful session termination
- **`CLIST`** - List all available courses with enrollment status
- **`SCHED`** - View personal schedule and waitlisted courses
- **`ENROLL`** - Enroll in available courses
- **`DROP`** - Drop enrolled courses
- **`WAIT`** - Join waitlist for full courses

### Advanced Features
- **Thread-Safe Operations** - Comprehensive mutex-based synchronization
- **Signal Handling** - Graceful shutdown with SIGINT
- **Memory Management** - Leak-free dynamic allocation
- **Error Handling** - Robust client and server error recovery
- **Reconnection Support** - Users can reconnect to existing sessions

## 🏗️ Architecture

```
┌─────────────────┐    ┌──────────────────┐    ┌─────────────────┐
│   Main Server   │───▶│   Thread Pool    │───▶│ Client Handler  │
│   (Accept Loop) │    │  (Per Client)    │    │   (Protocol)    │
└─────────────────┘    └──────────────────┘    └─────────────────┘
         │                       │                       │
         ▼                       ▼                       ▼
┌─────────────────┐    ┌──────────────────┐    ┌─────────────────┐
│ Course Manager  │    │  User Manager    │    │ Logger System   │
│ (Enrollment LL) │    │ (Session Track)  │    │ (Thread-Safe)   │
└─────────────────┘    └──────────────────┘    └─────────────────┘
         │                       │                       │
         ▼                       ▼                       ▼
┌─────────────────┐    ┌──────────────────┐    ┌─────────────────┐
│   Mutex Layer   │    │  Protocol Layer  │    │ Signal Handler  │
│ (Thread Safety) │    │ (Binary Msgs)    │    │ (Graceful Exit) │
└─────────────────┘    └──────────────────┘    └─────────────────┘
```

## 🛠️ Building

### Prerequisites
- **GCC Compiler** with C99 support
- **POSIX Threads** (`libpthread`)
- **Unix/Linux System** (tested on Linux)

### Compilation
```bash
# Standard build
make

# Clean build artifacts
make clean
```

### Output
- **Executable**: `bin/zotReg_server`
- **Protocol Library**: `lib/protocol.o` (pre-compiled)

## 📖 Usage

### Starting the Server
```bash
./bin/zotReg_server <PORT> <COURSE_FILE> <LOG_FILE>
```

**Parameters:**
- `PORT` - Port number to listen on (e.g., 3200)
- `COURSE_FILE` - Course data file (see format below)
- `LOG_FILE` - Server activity log file

### Example
```bash
./bin/zotReg_server 3200 rsrc/course_1.txt server.log
```

### Course File Format
```
Course Title;Max Capacity
Let's Eat! French;2
Typing;10
Guitar I;15
```

### Server Output
```
Server initialized with 3 courses.
Currently listening on port 3200.
```

## 🔌 Client Protocol

### Message Format
```c
typedef struct {
    uint32_t msg_len;   // Message length (network byte order)
    uint8_t msg_type;   // Message type (see below)
} petrV_header;
```

### Message Types
| Type | Value | Description | Payload |
|------|-------|-------------|---------|
| `LOGIN` | 1 | User authentication | Username string |
| `LOGOUT` | 2 | Session termination | None |
| `CLIST` | 3 | Course listing request | None |
| `SCHED` | 4 | Personal schedule request | None |
| `ENROLL` | 5 | Course enrollment | Course index |
| `DROP` | 6 | Course drop | Course index |
| `WAIT` | 7 | Join waitlist | Course index |

### Response Types
| Type | Value | Description |
|------|-------|-------------|
| `OK` | 0 | Operation successful |
| `EUSRLGDIN` | 240 | User already logged in |
| `ECDENIED` | 241 | Operation denied |
| `ECNOTFOUND` | 242 | Course not found |
| `ENOCOURSES` | 243 | No courses enrolled |
| `ESERV` | 255 | Server error |

### Example Client Session
```bash
# Connect to server
telnet localhost 3200

# Login (binary protocol - use provided client)
LOGIN: "alice"
Response: OK

# List courses
CLIST
Response: "Course 0 - Let's Eat! French\nCourse 1 - Typing\nCourse 2 - Guitar I (CLOSED)\n"

# Enroll in course
ENROLL: "0"
Response: OK

# View schedule
SCHED
Response: "Course 0 - Let's Eat! French\n"

# Logout
LOGOUT
Response: OK
```

## 🔧 Technical Implementation

### Core Data Structures

#### User Management
```c
typedef struct {
    char* username;           // User identifier
    int socket_fd;           // Client socket descriptor
    pthread_t tid;           // Thread ID
    enrollment_t courses;    // Enrollment bitmasks
    pthread_mutex_t user_lock;    // User data protection
    pthread_mutex_t read_lock;    // Reader-writer pattern
    int read_count;          // Active readers count
} user_t;
```

#### Course Management
```c
typedef struct {
    char* title;             // Course name
    int maxCap;             // Maximum enrollment
    dlist_t enrollment;     // Enrolled users (sorted)
    dlist_t waitlist;       // Waitlisted users (FIFO)
} course_t;
```

#### Enrollment Tracking
```c
typedef struct {
    uint32_t enrolled;      // Bitmask of enrolled courses
    uint32_t waitlisted;    // Bitmask of waitlisted courses
} enrollment_t;
```

### Thread Synchronization

#### Global Locks
```c
pthread_mutex_t num_accepted_connections_lock;  // Server statistics
pthread_mutex_t successful_client_logins_lock;  // Login counter
pthread_mutex_t num_attempted_enrollments_lock; // Enrollment attempts
pthread_mutex_t num_successful_drops_lock;      // Drop counter
pthread_mutex_t enrollment_log_lock;            // Log file access
pthread_mutex_t courseArrayLocks[32];           // Per-course protection
```

#### User-Level Locks
- **`user_lock`** - Protects user data modifications
- **`read_lock`** - Implements reader-writer pattern for user data access
- **`read_count`** - Tracks concurrent readers

### Key Functions

| Function | Purpose | Location |
|----------|---------|----------|
| `server_startup()` | Initialize TCP server socket | `src/helper.c` |
| `populate_courses()` | Load course data from file | `src/helper.c` |
| `client_work()` | Main client thread handler | `src/server.c` |
| `rd_msgheader()` | Read protocol message header | `lib/protocol.o` |
| `wr_msg()` | Write protocol message | `lib/protocol.o` |
| `sigint_handler()` | Graceful shutdown handler | `src/helper.c` |

### Process Flow

1. **Server Initialization**
   - Load course data from file
   - Initialize mutex locks
   - Create listening socket
   - Install signal handlers

2. **Client Connection**
   - Accept incoming connections
   - Validate LOGIN message
   - Create user session or reconnect
   - Spawn client thread

3. **Client Thread Lifecycle**
   - Process client requests in loop
   - Maintain thread-safe operations
   - Log all activities
   - Handle graceful disconnection

4. **Graceful Shutdown**
   - Signal all client threads
   - Wait for thread completion
   - Output final statistics
   - Clean up resources

## 🧪 Testing

### Manual Testing
```bash
# Start server
./bin/zotReg_server 3200 rsrc/course_1.txt test.log

# Use telnet or custom client to test operations
# Check log file for activity tracking
cat test.log
```

### Memory Testing
```bash
# Run with Valgrind
valgrind --tool=memcheck ./bin/zotReg_server 3200 rsrc/course_1.txt test.log
```

### Load Testing
```bash
# Multiple concurrent clients
for i in {1..10}; do
    (echo "Testing client $i" | nc localhost 3200) &
done
```

## 📁 Project Structure

```
hw5/
├── include/
│   ├── server.h          # Server configuration and constants
│   ├── helper.h          # Helper function prototypes and structures
│   ├── protocol.h        # Network protocol definitions
│   ├── dlinkedlist.h     # Doubly-linked list interface
│   ├── mlinkedlist.h     # Modified linked list (unused)
│   └── debug.h           # Debug macros and color output
├── src/
│   ├── server.c          # Main server logic and client handling
│   ├── helper.c          # Utility functions and server setup
│   ├── dlinkedlist.c     # Doubly-linked list implementation
│   └── mlinkedlist.c     # Modified linked list (unused)
├── lib/
│   └── protocol.o        # Pre-compiled protocol library
├── rsrc/
│   └── course_1.txt      # Sample course data file
├── bin/                  # Compiled executable (created by make)
├── Makefile              # Build configuration
├── log.txt               # Server activity log (example)
├── err.txt               # Valgrind output (example)
└── README.md             # This file
```

## 🔍 Implementation Details

### Thread Safety Mechanisms

#### Reader-Writer Pattern
```c
// Read operation example
pthread_mutex_lock(&user->read_lock);
user->read_count++;
if(user->read_count == 1) {
    pthread_mutex_lock(&user->user_lock);  // First reader locks
}
pthread_mutex_unlock(&user->read_lock);

// Critical section - reading user data

pthread_mutex_lock(&user->read_lock);
user->read_count--;
if(user->read_count == 0) {
    pthread_mutex_unlock(&user->user_lock);  // Last reader unlocks
}
pthread_mutex_unlock(&user->read_lock);
```

#### Deadlock Prevention
- **Lock Ordering** - Consistent acquisition order across threads
- **Timeout Mechanisms** - Prevent indefinite blocking
- **Fine-Grained Locking** - Minimize critical section size

### Memory Management
```c
// Proper cleanup on server shutdown
node_t * cleanup_ptr = users->head;
while(cleanup_ptr) {
    user_t * user = (user_t*)cleanup_ptr->data;
    node_t * next = cleanup_ptr->next;
    free(user->username);
    free(user);
    free(cleanup_ptr);
    cleanup_ptr = next;
}
```

### Error Handling
- **Network Errors** - Handle client disconnections gracefully
- **Resource Exhaustion** - Manage memory allocation failures
- **Invalid Requests** - Validate all client input
- **Concurrent Access** - Prevent race conditions

## 📊 Server Statistics

### Runtime Statistics
The server tracks and reports:
- **Total Connections** - Number of client connections accepted
- **Successful Logins** - Number of successful authentication attempts
- **Enrollment Attempts** - Total course enrollment requests
- **Successful Drops** - Number of courses successfully dropped

### Final Output Format
```
Course Statistics:
<Course Title>, <Max Capacity>, <Current Enrollment>, <Enrolled Users>, <Waitlisted Users>

User Statistics:
<Username>, <Enrolled Bitmask>, <Waitlisted Bitmask>

Server Statistics:
<Connections>, <Logins>, <Enrollments>, <Drops>
```

### Example Output
```
Let's Eat! French, 2, 1, alice, bob;charlie
Typing, 10, 0, , 
Guitar I, 15, 2, alice;bob, charlie

alice, 5, 0
bob, 4, 1
charlie, 0, 3

3, 3, 2, 1
```

## 🎯 Key Features Demonstrated

### Systems Programming Concepts
- **Multi-threading** - pthread creation, synchronization, cleanup
- **Network Programming** - TCP sockets, client-server architecture
- **Concurrent Data Structures** - Thread-safe linked lists
- **Inter-Process Communication** - Custom binary protocol
- **Signal Handling** - Graceful shutdown and resource cleanup

### Advanced Threading
- **Mutex Programming** - Deadlock prevention, fine-grained locking
- **Reader-Writer Patterns** - Optimized concurrent access
- **Thread Pool Management** - Dynamic thread creation per client
- **Condition Variables** - Efficient thread coordination

### Data Structure Design
- **Doubly-Linked Lists** - O(1) insertion/deletion with function pointers
- **Bitmask Operations** - Efficient enrollment tracking
- **Hash-like Access** - Course array indexing for O(1) access
- **Generic Data Structures** - Reusable linked list implementation

## ⚠️ Known Limitations

- **Maximum Courses** - Limited to 32 courses (bitmask constraint)
- **Memory Leaks** - Minor leaks on shutdown (74 bytes reported by Valgrind)
- **Error Recovery** - Limited client reconnection on network errors
- **Scalability** - One thread per client may not scale to thousands of users
- **Persistence** - No database backing; all data lost on restart

## 🏆 Educational Value

This implementation demonstrates mastery of:
- **Advanced Threading** - Complex synchronization with multiple lock types
- **Network Server Design** - Production-quality client-server architecture
- **Protocol Design** - Custom binary communication protocol
- **Resource Management** - Careful memory and file descriptor handling
- **Concurrent Programming** - Race condition prevention and deadlock avoidance
- **Software Engineering** - Modular design with clear separation of concerns

Perfect for understanding enterprise-level server architecture and gaining hands-on experience with complex multi-threaded systems programming.

## 🐛 Debugging

### Common Issues
- **Port already in use** - Change port number or wait for socket timeout
- **Permission denied** - Use ports > 1024 for non-root users
- **Segmentation faults** - Check Valgrind output for memory errors
- **Deadlocks** - Review lock acquisition order

### Debug Build
```bash
# Enable debug output
make debug
export DEBUG=1
./bin/zotReg_server 3200 rsrc/course_1.txt debug.log
```

---

**Development Environment**: C99, GCC, POSIX Threads  
**Dependencies**: `libpthread`, POSIX sockets  
**Course**: ICS 53 - Principles in System Design  
**Assignment**: Multi-threaded Server Implementation 