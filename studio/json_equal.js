.pragma library

// Parsed status objects compare equal when their JSON text matches.
// Key order follows the backend, so two identical replies stay identical.
function same(a, b) {
    return JSON.stringify(a) === JSON.stringify(b);
}
