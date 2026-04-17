/* Prism Standard Library — testing module
   Lightweight test framework inspired by Jest / pytest.
*/

let _suites = []
let _current_suite = void
let _passed = 0
let _failed = 0
let _errors = []

func describe(name, suite_fn) {
    _current_suite = name
    push(_suites, name)
    output(f"\n  Suite: {name}")
    suite_fn()
    _current_suite = void
}

func it(name, test_fn) {
    try {
        test_fn()
        _passed += 1
        output(f"    ✓ {name}")
    } catch (err) {
        _failed += 1
        push(_errors, {"suite": _current_suite ?? "root", "test": name, "error": str(err)})
        output(f"    ✗ {name}")
        output(f"      → {err}")
    }
}

func test(name, test_fn) {
    return it(name, test_fn)
}

func expect(actual) {
    return {
        "toBe": fn(expected) {
            if actual != expected {
                error(f"Expected {str(expected)}, got {str(actual)}")
            }
        },
        "toEqual": fn(expected) {
            if str(actual) != str(expected) {
                error(f"Expected {str(expected)}, got {str(actual)}")
            }
        },
        "toBeTrue": fn() {
            if actual != true {
                error(f"Expected true, got {str(actual)}")
            }
        },
        "toBeFalse": fn() {
            if actual != false {
                error(f"Expected false, got {str(actual)}")
            }
        },
        "toBeNull": fn() {
            if actual != void {
                error(f"Expected void/null, got {str(actual)}")
            }
        },
        "toBeGreaterThan": fn(n) {
            if actual <= n {
                error(f"Expected {str(actual)} > {str(n)}")
            }
        },
        "toBeLessThan": fn(n) {
            if actual >= n {
                error(f"Expected {str(actual)} < {str(n)}")
            }
        },
        "toContain": fn(item) {
            if not contains(str(actual), str(item)) {
                error(f"Expected to contain {str(item)}")
            }
        },
        "toHaveLength": fn(n) {
            if len(actual) != n {
                error(f"Expected length {str(n)}, got {str(len(actual))}")
            }
        },
        "toThrow": fn(callable) {
            let threw = false
            try {
                callable()
            } catch (e) {
                threw = true
            }
            if not threw {
                error("Expected function to throw an error")
            }
        },
    }
}

func beforeAll(setup_fn) { setup_fn() }
func afterAll(teardown_fn)  { teardown_fn() }
func beforeEach(setup_fn) { setup_fn() }
func afterEach(teardown_fn)  { teardown_fn() }

func summary() {
    output(f"\n══ Test Results ══")
    output(f"  Passed: {str(_passed)}")
    output(f"  Failed: {str(_failed)}")
    output(f"  Total:  {str(_passed + _failed)}")
    if _failed > 0 {
        output("\nFailures:")
        for e in _errors {
            let eSuite = e["suite"]
            let eTest  = e["test"]
            let eErr   = e["error"]
            output(f"  [{eSuite}] {eTest}")
            output(f"    {eErr}")
        }
    }
    return _failed == 0
}

func reset() {
    _passed = 0
    _failed = 0
    _errors = []
    _suites = []
}
