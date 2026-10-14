# Where does the backend seam go?

Type: grilling
Status: open
Blocked by: 01, 02

## Question

What is the shape of the seam between FenrirPhysics and its physics backend?

- Is the backend chosen at run time (an interface FenrirPhysics calls through) or at build time (one backend compiled in, behind private headers)?
- What vocabulary does FenrirPhysics own (body, shape, collider, material, layer, body handle), and how does it map onto Jolt's without depending on it?
- How are backend IDs and lifetimes kept out of the public interface (opaque handles, PIMPL)?
- Is there a test-only fake backend, so FenrirPhysics can be tested without Jolt, or are its tests run against Jolt?

Builds on "What does Jolt require of a host engine?" (01) and "How do other engines hide their physics backend?" (02). Likely an ADR: it is hard to reverse, and it records the compile-level hiding that goes beyond ADR-0008.
