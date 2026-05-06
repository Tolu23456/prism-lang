# Data Structures Library Documentation

Advanced data structure implementations for Prism.

## Import

```prism
import "lib/datastructures"
```

## MinHeap / MaxHeap

Priority queue data structures for efficient min/max element access.

### MinHeap

```prism
let heap = MinHeap()
heap.push(5)
heap.push(3)
heap.push(7)
print(heap.pop())  // 3
```

### MaxHeap

```prism
let heap = MaxHeap()
heap.push(5)
heap.push(3)
heap.push(7)
print(heap.pop())  // 7
```

## Trie

Prefix tree for fast string lookups and autocomplete.

```prism
let trie = Trie()
trie.insert("hello")
trie.insert("help")
trie.insert("world")

print(trie.search("hello"))      // true
print(trie.startsWith("hel"))    // true
print(trie.delete("hello"))      // true
print(trie.search("hello"))      // false
```

## Graph

Graph data structure with BFS, DFS, and Dijkstra algorithms.

```prism
let graph = Graph()
graph.addEdge("A", "B", 1)
graph.addEdge("B", "C", 2)
graph.addEdge("A", "C", 5)

let paths = graph.dijkstra("A")
print(paths["C"])  // 3 (A->B->C)
```

## UnionFind

Disjoint set union with path compression.

```prism
let uf = UnionFind(10)
uf.union(1, 2)
uf.union(2, 3)
print(uf.find(1) == uf.find(3))  // true
```

## BloomFilter

Probabilistic set membership testing.

```prism
let filter = BloomFilter(100, 3)
filter.add("apple")
filter.add("banana")

print(filter.might_contain("apple"))    // true
print(filter.might_contain("cherry"))   // false (probably)
```

## SegmentTree

Range query and update structure.

```prism
let tree = SegmentTree([1, 2, 3, 4, 5])
print(tree.rangeSum(0, 2))  // 6 (1+2+3)
tree.update(1, 5)
print(tree.rangeSum(0, 2))  // 9 (1+5+3)
```

## Stack and Queue

LIFO and FIFO data structures.

```prism
let stack = Stack()
stack.push(1)
stack.push(2)
print(stack.pop())  // 2

let queue = Queue()
queue.enqueue(1)
queue.enqueue(2)
print(queue.dequeue())  // 1
```

## Performance Characteristics

| Structure | Insert | Delete | Search |
|-----------|--------|--------|--------|
| MinHeap | O(log n) | O(log n) | O(1) |
| Trie | O(m) | O(m) | O(m) |
| Graph | O(1) | O(V+E) | O(V+E) |
| UnionFind | O(α(n)) | - | O(α(n)) |
| BloomFilter | O(k) | - | O(k) |
| SegmentTree | O(log n) | O(log n) | O(log n) |

*m = string length, V = vertices, E = edges, k = hash functions, α(n) ≈ constant*
