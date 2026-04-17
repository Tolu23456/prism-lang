# Classes & Structs in Prism

## Classes

Classes provide object-oriented programming with methods, inheritance,
and encapsulation.

```prism
class Animal {
    func init(name, sound) {
        self.name  = name
        self.sound = sound
        self.alive = true
    }

    func speak() {
        output "{self.name} says {self.sound}!"
    }

    func describe() {
        return "I am {self.name}, a {type(self)}"
    }
}

let a = new Animal("Cat", "meow")
a.speak()                    # Cat says meow!
output(a.describe())
output(a.name)               # Cat
a.name = "Kitty"             # field mutation
```

### Constructor

`init` is the constructor — called automatically by `new`:

```prism
class Point {
    func init(x, y) {
        self.x = float(x)
        self.y = float(y)
    }
    func length() {
        return sqrt(self.x * self.x + self.y * self.y)
    }
}

let p = new Point(3, 4)
output(p.length())   # 5.0
```

### Inheritance

```prism
class Shape {
    func init(color) {
        self.color = color
    }
    func area() { return 0 }
    func describe() {
        output "A {self.color} shape with area {self.area()}"
    }
}

class Circle extends Shape {
    func init(color, radius) {
        super.init(color)
        self.radius = radius
    }
    func area() {
        return 3.14159 * self.radius * self.radius
    }
}

class Rectangle extends Shape {
    func init(color, w, h) {
        super.init(color)
        self.w = w
        self.h = h
    }
    func area() { return self.w * self.h }
}

let c = new Circle("red", 5)
let r = new Rectangle("blue", 4, 6)

c.describe()   # A red shape with area 78.53...
r.describe()   # A blue shape with area 24
```

### `super`

`super.method(args)` calls the parent class method:

```prism
class Dog extends Animal {
    func init(name) {
        super.init(name, "woof")
        self.breed = "unknown"
    }
    func fetch(item) {
        output "{self.name} fetches the {item}!"
        return self
    }
}

let d = new Dog("Rex")
d.speak()          # Rex says woof!
d.fetch("ball")    # Rex fetches the ball!
```

### Method Chaining

Methods can return `self` to support fluent APIs:

```prism
class Builder {
    func init() {
        self.parts = []
    }
    func add(part) {
        push(self.parts, part)
        return self
    }
    func build() {
        return join(", ", self.parts)
    }
}

let result = new Builder()
    .add("wheels")
    .add("engine")
    .add("body")
    .build()
output(result)   # wheels, engine, body
```

### Class as Namespace

```prism
class Math {
    func init() {}

    func gcd(a, b) {
        while b != 0 {
            let t = b
            b = a % b
            a = t
        }
        return a
    }

    func lcm(a, b) {
        return (a * b) // self.gcd(a, b)
    }
}

let m = new Math()
output(m.gcd(12, 8))    # 4
output(m.lcm(12, 8))    # 24
```

---

## Structs

Structs are lightweight value-types with positional fields and no methods.

```prism
struct Point { x, y }
struct Color { r, g, b, a }
struct Rect  { x, y, width, height }
```

Create instances with `new`:

```prism
let p = new Point(10, 20)
output(p.x)   # 10
output(p.y)   # 20
p.x = 15      # field mutation
```

Structs are ideal for simple data containers where methods aren't needed:

```prism
struct Employee { name, department, salary }

let emp = new Employee("Alice", "Engineering", 95000)
output("{emp.name} works in {emp.department}")

# Use in collections
let team = [
    new Employee("Alice", "Eng", 95000),
    new Employee("Bob",   "Eng", 88000),
    new Employee("Carol", "PM",  92000)
]

for e in team {
    output(f"{e.name}: ${e.salary}")
}
```

---

## Duck Typing & Polymorphism

Prism uses structural / duck typing — any object with the right methods works:

```prism
func make_sound(animal) {
    animal.speak()
}

class Cat {
    func init() { self.name = "Cat" }
    func speak() { output "Meow!" }
}

class Dog {
    func init() { self.name = "Dog" }
    func speak() { output "Woof!" }
}

make_sound(new Cat())   # Meow!
make_sound(new Dog())   # Woof!
```

---

## Type Checking

Use `is` for type and class checks:

```prism
let x = new Point(1, 2)
output(x is Point)    # true (by type name match)
output(type(x))       # "Point" (class name)

if x is Point {
    output "it's a point"
}
```
