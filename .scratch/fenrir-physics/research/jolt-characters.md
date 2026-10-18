# What Jolt offers for characters

Research for [30: What does Jolt offer for characters?](../issues/30-what-jolt-offers-for-characters.md).

**Pinned source:** JoltPhysics **v5.6.0**, commit `e77f175595e64cb44218cc9d9d56fc365ad0e36a` (tagged 2026-07-11), the submodule at `packages/FenrirPhysics/libs/JoltPhysics`. Every link points at that tag. Sources are Jolt's own headers and `.cpp` files under `Jolt/Physics/Character/` and `Jolt/Physics/Collision/`, `Docs/Architecture.md`, `Docs/ReleaseNotes.md`, `Samples/Tests/Character/`, `UnitTests/Physics/CharacterVirtualTests.cpp` and `PerformanceTest/CharacterVirtualScene.h`. A few FenrirPhysics facts come from `packages/FenrirPhysics/src/Backend/Jolt/JoltBackend.cpp` and `src/PhysicsWorld.cpp` on `main` at `2e08410`.

Link shorthand: `Character/` is `Jolt/Physics/Character/`, `Collision/` is `Jolt/Physics/Collision/`, `Tests/` is `Samples/Tests/Character/`. `Architecture.md` is [`Docs/Architecture.md`](https://github.com/jrouwe/JoltPhysics/blob/v5.6.0/Docs/Architecture.md); Markdown line links use `?plain=1`. Statements marked *(inference)* are read off the code paths, not stated by Jolt.

---

## 1. The two characters

Both derive from `CharacterBase`, which holds the shared settings (up vector, supporting volume, max slope, shape) and the ground state ([CharacterBase.h L19-L159](https://github.com/jrouwe/JoltPhysics/blob/v5.6.0/Jolt/Physics/Character/CharacterBase.h#L19-L159)).

**`Character`: a rigid body kept upright.**
- The constructor creates one body with motion type **`Dynamic`** (not kinematic), mass from the settings (`MassAndInertiaProvided`, default 80 kg), friction 0.2, a gravity factor, and allowed DOFs of translation X, Y, Z only, so it cannot rotate ([Character.h L16-L40](https://github.com/jrouwe/JoltPhysics/blob/v5.6.0/Jolt/Physics/Character/Character.h#L16-L40); [Character.cpp L31-L47](https://github.com/jrouwe/JoltPhysics/blob/v5.6.0/Jolt/Physics/Character/Character.cpp#L31-L47)). The header still calls it "a keyframed object" controlled by setting the velocity ([Character.h L42-L45](https://github.com/jrouwe/JoltPhysics/blob/v5.6.0/Jolt/Physics/Character/Character.h#L42-L45)).
- The host adds and removes it (`AddToPhysicsSystem`, `RemoveFromPhysicsSystem`) and **must call `PostSimulation` after every `PhysicsSystem::Update`** to refresh the ground state ([Character.h L62-L74](https://github.com/jrouwe/JoltPhysics/blob/v5.6.0/Jolt/Physics/Character/Character.h#L62-L74)).
- It is simulated with everything else: gravity, contacts, being pushed, friction on platforms all come from the solver. Every accessor is a thin wrapper over `BodyInterface` with an `inLockBodies` switch ([Character.cpp L16-L29, L217-L287](https://github.com/jrouwe/JoltPhysics/blob/v5.6.0/Jolt/Physics/Character/Character.cpp#L16-L29)).
- It has **no** slope blocking, stair stepping, floor sticking or character contact listener. The max slope angle only classifies the ground ([Character.cpp L194-L201](https://github.com/jrouwe/JoltPhysics/blob/v5.6.0/Jolt/Physics/Character/Character.cpp#L194-L201)); the sample cancels input towards steep ground itself ([CharacterTest.cpp L73-L86](https://github.com/jrouwe/JoltPhysics/blob/v5.6.0/Samples/Tests/Character/CharacterTest.cpp#L73-L86)).

**`CharacterVirtual`: collide-and-slide, not a body.**
- It "doesn't use a rigid body but moves doing collision checks only". It is not tracked by the `PhysicsSystem`, so the host updates it and saves its state; `PhysicsSystem::SaveState` does not include it ([CharacterVirtual.h L262-L268](https://github.com/jrouwe/JoltPhysics/blob/v5.6.0/Jolt/Physics/Character/CharacterVirtual.h#L262-L268)).
- Position, rotation and velocity are plain members set by the host (`SetPosition`, `SetRotation`, `SetLinearVelocity`), with position at the **base** of the shape, not the centre of mass ([CharacterVirtual.h L299-L324, L720-L727](https://github.com/jrouwe/JoltPhysics/blob/v5.6.0/Jolt/Physics/Character/CharacterVirtual.h#L299-L324)).
- Extra features over `Character`: sliding along walls, moving platforms, funnel-aware steep-slope detection, stair stepping and floor sticking through `ExtendedUpdate`, and a local frame for moving ships ([Architecture.md L711-L717](https://github.com/jrouwe/JoltPhysics/blob/v5.6.0/Docs/Architecture.md?plain=1#L711-L717)).

**What the docs recommend.** `Character` "is usually not the best solution for a player as the player usually requires a lot of behavior that is non-physical"; it is cheap and "recommended for e.g. simple AI characters". `CharacterVirtual` "is much more advanced" and "should provide everything that Character provides" ([Architecture.md L705, L709, L719](https://github.com/jrouwe/JoltPhysics/blob/v5.6.0/Docs/Architecture.md?plain=1#L705-L719)). Both are "usually driven in a kinematic way", by setting the linear velocity before their update ([Architecture.md L707](https://github.com/jrouwe/JoltPhysics/blob/v5.6.0/Docs/Architecture.md?plain=1#L707)).

**The optional inner body of `CharacterVirtual`.** Without it, a `CharacterVirtual` is invisible to the world:
- rigid bodies do not see it; it "only interacts with them during the CharacterVirtual::Update function by applying impulses" ([Architecture.md L709](https://github.com/jrouwe/JoltPhysics/blob/v5.6.0/Docs/Architecture.md?plain=1#L709));
- queries (`CastRay` etc.) do not hit it, because it is not in the broad phase; the documented workaround is a second query against `GetTransformedShape()` ([Architecture.md L723](https://github.com/jrouwe/JoltPhysics/blob/v5.6.0/Docs/Architecture.md?plain=1#L723); [CharacterVirtual.h L58, L474-L475](https://github.com/jrouwe/JoltPhysics/blob/v5.6.0/Jolt/Physics/Character/CharacterVirtual.h#L58));
- sensor overlaps are reported only to the character's own `CharacterContactListener`, never to the regular `ContactListener` ([Architecture.md L720-L721](https://github.com/jrouwe/JoltPhysics/blob/v5.6.0/Docs/Architecture.md?plain=1#L720-L721)).

Setting `mInnerBodyShape` creates a **kinematic** body in `mInnerBodyLayer` that follows the character. Then regular queries and the regular `ContactListener` see it, and fast `LinearCast` bodies cannot tunnel through the character ([CharacterVirtual.h L57-L68](https://github.com/jrouwe/JoltPhysics/blob/v5.6.0/Jolt/Physics/Character/CharacterVirtual.h#L57-L68); [Architecture.md L725](https://github.com/jrouwe/JoltPhysics/blob/v5.6.0/Docs/Architecture.md?plain=1#L725)). Details:
- The constructor creates **and adds** it with `mAllowSleeping = false` ("so that we will receive sensor callbacks") and the character's user data; the destructor removes and destroys it. An `mInnerBodyIDOverride` lets the host pick its `BodyID` "for a deterministic simulation" ([CharacterVirtual.cpp L142-L170](https://github.com/jrouwe/JoltPhysics/blob/v5.6.0/Jolt/Physics/Character/CharacterVirtual.cpp#L142-L170); [CharacterVirtual.h L63-L65](https://github.com/jrouwe/JoltPhysics/blob/v5.6.0/Jolt/Physics/Character/CharacterVirtual.h#L63-L65)).
- It is moved by `SetPositionAndRotation(..., DontActivate)` after each `Update` and on `SetPosition`, `SetRotation` and `SetShapeOffset`, to `position + rotation * shapeOffset + padding * up` ([CharacterVirtual.cpp L172-L176, L1470-L1471](https://github.com/jrouwe/JoltPhysics/blob/v5.6.0/Jolt/Physics/Character/CharacterVirtual.cpp#L172-L176); [CharacterVirtual.h L680-L684](https://github.com/jrouwe/JoltPhysics/blob/v5.6.0/Jolt/Physics/Character/CharacterVirtual.h#L680-L684)). Nothing sets its velocity, so it is a teleported kinematic body with zero velocity *(inference)*.
- The character's own queries always skip it (`IgnoreSingleBodyFilterChained(mInnerBodyID, ...)`, [CharacterVirtual.cpp L399, L421, L591, L677](https://github.com/jrouwe/JoltPhysics/blob/v5.6.0/Jolt/Physics/Character/CharacterVirtual.cpp#L399)).
- Its shape is **not** changed by `SetShape`; the host calls `SetInnerBodyShape` after a successful switch ([CharacterVirtual.h L471-L472](https://github.com/jrouwe/JoltPhysics/blob/v5.6.0/Jolt/Physics/Character/CharacterVirtual.h#L471-L472); [CharacterVirtualTest.cpp L177-L186](https://github.com/jrouwe/JoltPhysics/blob/v5.6.0/Samples/Tests/Character/CharacterVirtualTest.cpp#L177-L186)). The samples make the inner shape 0.9 times the outer one ([CharacterBaseTest.h L74](https://github.com/jrouwe/JoltPhysics/blob/v5.6.0/Samples/Tests/Character/CharacterBaseTest.h#L74); [CharacterBaseTest.cpp L106-L109](https://github.com/jrouwe/JoltPhysics/blob/v5.6.0/Samples/Tests/Character/CharacterBaseTest.cpp#L106-L109)).

**Against FenrirPhysics triggers.** A FenrirPhysics trigger is a kinematic sensor kept awake with `mCollideKinematicVsNonDynamic` ([JoltBackend.cpp L1374-L1391](../../../packages/FenrirPhysics/src/Backend/Jolt/JoltBackend.cpp)). Jolt sensors report contacts only with bodies ([Architecture.md L398-L408](https://github.com/jrouwe/JoltPhysics/blob/v5.6.0/Docs/Architecture.md?plain=1#L398-L408)), so a trigger's own `ContactListener` events fire for a `CharacterVirtual` only through an inner body. Without one, the only report is the character-side `OnContactAdded` (section 5).

## 2. Settings

`CharacterBaseSettings` ([CharacterBase.h L33-L49](https://github.com/jrouwe/JoltPhysics/blob/v5.6.0/Jolt/Physics/Character/CharacterBase.h#L33-L49)):

| Setting | Default | Meaning | Live? |
|---|---|---|---|
| `mUp` | +Y | Character's up axis | `SetUp` |
| `mSupportingVolume` | `Plane(Y, -1e10)` (accept all) | Local-space plane; contacts behind it can support, contacts in front only collide | `SetSupportingVolume` |
| `mMaxSlopeAngle` | 50 degrees | Walkable slope limit; a cosine above 0.9999 turns the check off ([L145](https://github.com/jrouwe/JoltPhysics/blob/v5.6.0/Jolt/Physics/Character/CharacterBase.h#L145)) | `SetMaxSlopeAngle` |
| `mEnhancedInternalEdgeRemoval` | false | Extra ghost-edge removal, costlier | `CharacterVirtual` only (`SetEnhancedInternalEdgeRemoval`) |
| `mShape` | none | Initial shape; "make sure the shape is made so that the bottom of the shape is at (0, 0, 0)" | `SetShape` (section 8) |

All samples set the supporting volume to `Plane(Y, -radius)`, so only contacts on the capsule's lower sphere count as ground ([CharacterVirtualTest.cpp L35](https://github.com/jrouwe/JoltPhysics/blob/v5.6.0/Samples/Tests/Character/CharacterVirtualTest.cpp#L35); [CharacterTest.cpp L33](https://github.com/jrouwe/JoltPhysics/blob/v5.6.0/Samples/Tests/Character/CharacterTest.cpp#L33)). The samples also use a 45 degree max slope, not the 50 degree default.

`CharacterSettings` adds `mLayer`, `mMass` (80), `mFriction` (0.2), `mGravityFactor` (1) and `mAllowedDOFs` ([Character.h L26-L39](https://github.com/jrouwe/JoltPhysics/blob/v5.6.0/Jolt/Physics/Character/Character.h#L26-L39)). Live: `SetLayer`, `SetShape`. Mass, friction and gravity factor have no `Character` setter; they live on the body ([Character.cpp L333-L352](https://github.com/jrouwe/JoltPhysics/blob/v5.6.0/Jolt/Physics/Character/Character.cpp#L333-L352)).

`CharacterVirtualSettings` ([CharacterVirtual.h L33-L68](https://github.com/jrouwe/JoltPhysics/blob/v5.6.0/Jolt/Physics/Character/CharacterVirtual.h#L33-L68); setters [L326-L372](https://github.com/jrouwe/JoltPhysics/blob/v5.6.0/Jolt/Physics/Character/CharacterVirtual.h#L326-L372)):

| Setting | Default | Meaning | Live? |
|---|---|---|---|
| `mID` | next global ID | Sort key and identity in removal callbacks | no |
| `mMass` | 70 kg | Pushes down what it stands on | `SetMass` |
| `mMaxStrength` | 100 N | Max force when pushing bodies | `SetMaxStrength` |
| `mShapeOffset` | 0 | Extra local offset of the shape; "setting it on the fly can cause the shape to teleport into collision" | `SetShapeOffset` |
| `mBackFaceMode` | `CollideWithBackFaces` | Back-facing triangles block | no |
| `mPredictiveContactDistance` | 0.1 m | How far outside the shape contacts are gathered; 0 "will most likely cause the character to get stuck", too high gives ghost collisions | no |
| `mMaxCollisionIterations` | 5 | Outer collide-and-slide loops | no |
| `mMaxConstraintIterations` | 15 | Inner solver iterations | no |
| `mMinTimeRemaining` | 1e-4 s | Early-out | no |
| `mCollisionTolerance` | 1e-3 m | Allowed penetration; also the "touching" threshold for ground | no |
| `mCharacterPadding` | 0.02 m | Gap kept from geometry | getter only |
| `mMaxNumHits` | 256 | Contacts kept per query | `SetMaxNumHits` |
| `mHitReductionCosMaxAngle` | 0.999 | Merges near-duplicate contacts past the limit | `SetHitReductionCosMaxAngle` |
| `mPenetrationRecoverySpeed` | 1.0 | Fraction of penetration resolved per update (1 = all at once) | `SetPenetrationRecoverySpeed` |
| `mInnerBodyShape`, `mInnerBodyIDOverride`, `mInnerBodyLayer` | none, invalid, 0 | Section 1 | shape via `SetInnerBodyShape`; no layer setter (the inner body is an ordinary body, `GetInnerBodyID`) |

Also live: user data (`SetUserData`, which also updates the inner body's), listener, character-vs-character interface, position, rotation and velocity ([CharacterVirtual.h L290-L315, L363-L368](https://github.com/jrouwe/JoltPhysics/blob/v5.6.0/Jolt/Physics/Character/CharacterVirtual.h#L290-L315); [CharacterVirtual.cpp L1349-L1355](https://github.com/jrouwe/JoltPhysics/blob/v5.6.0/Jolt/Physics/Character/CharacterVirtual.cpp#L1349-L1355)). There is no gravity or slope-sliding setting. Gravity is passed per update (section 3).

The up vector is separate from the rotation. The planet sample sets both every tick ([CharacterPlanetTest.cpp L77-L83](https://github.com/jrouwe/JoltPhysics/blob/v5.6.0/Samples/Tests/Character/CharacterPlanetTest.cpp#L77-L83)), and the docs ask for `mUp` to be set when the world's up is not Y ([Architecture.md L744](https://github.com/jrouwe/JoltPhysics/blob/v5.6.0/Docs/Architecture.md?plain=1#L744)).

## 3. Moving

**`Update(dt, gravity, broadPhaseFilter, objectLayerFilter, bodyFilter, shapeFilter, tempAllocator)`** ([CharacterVirtual.h L385-L395](https://github.com/jrouwe/JoltPhysics/blob/v5.6.0/Jolt/Physics/Character/CharacterVirtual.h#L385-L395); [CharacterVirtual.cpp L1442-L1484](https://github.com/jrouwe/JoltPhysics/blob/v5.6.0/Jolt/Physics/Character/CharacterVirtual.cpp#L1442-L1484)):
- Does nothing if `dt <= 0`. Moves the character "according to its current velocity" by `MoveShape`, then recomputes the ground (`UpdateSupportingContact`), moves the inner body, and, if standing on a body with `mass > 0`, applies an impulse `mass * g * dt` (projected on the ground normal) to the ground body at the ground position.
- "It's your own responsibility to apply gravity to the character velocity!" The `gravity` argument is used **only** for that downward push ([CharacterVirtual.h L386, L389](https://github.com/jrouwe/JoltPhysics/blob/v5.6.0/Jolt/Physics/Character/CharacterVirtual.h#L386)).
- **`Update` never writes `mLinearVelocity`.** The only writes are `SetLinearVelocity` and the steep-slope cancel in `ExtendedUpdate` ([CharacterVirtual.cpp L1803](https://github.com/jrouwe/JoltPhysics/blob/v5.6.0/Jolt/Physics/Character/CharacterVirtual.cpp#L1803); [CharacterVirtual.h L303](https://github.com/jrouwe/JoltPhysics/blob/v5.6.0/Jolt/Physics/Character/CharacterVirtual.h#L303)). Sliding changes only the displacement. The samples measure the effective velocity as `(new_position - old_position) / dt` ([CharacterVirtualTest.cpp L104-L106](https://github.com/jrouwe/JoltPhysics/blob/v5.6.0/Samples/Tests/Character/CharacterVirtualTest.cpp#L104-L106)).
- Surface materials such as ice are left to the host: read the ground material and adjust the velocity or slope angle ([CharacterVirtual.h L387](https://github.com/jrouwe/JoltPhysics/blob/v5.6.0/Jolt/Physics/Character/CharacterVirtual.h#L387)).

**The algorithm (`MoveShape`).** For up to `mMaxCollisionIterations` ([CharacterVirtual.cpp L1263-L1347](https://github.com/jrouwe/JoltPhysics/blob/v5.6.0/Jolt/Physics/Character/CharacterVirtual.cpp#L1263-L1347)):
1. `GetContactsAtPosition`: one `CollideShape` with max separation `padding + predictiveContactDistance`, sorted, distances reduced by the padding (L415-L443).
2. Remove penetrating contacts with the same body and opposing normals (L445-L482).
3. Turn each contact into a plane constraint carrying the contact's velocity plus a penetration-recovery velocity; a steep contact adds a second, vertical plane (L702-L742).
4. Solve: walk the planes by time of impact, slide along one or two planes, call the listener, and push dynamic bodies (L815-L1070).
5. `ValidateMovement`: one `CastShape` along the solved displacement to catch anything the contacts missed, with the fraction corrected for padding (L569-L660).

**`ExtendedUpdate(dt, gravity, ExtendedUpdateSettings, four filters, tempAllocator)`** "combines Update, StickToFloor and WalkStairs" and "serves as an example of how these functions could be combined" ([CharacterVirtual.h L439-L451](https://github.com/jrouwe/JoltPhysics/blob/v5.6.0/Jolt/Physics/Character/CharacterVirtual.h#L439-L451); [CharacterVirtual.cpp L1796-L1886](https://github.com/jrouwe/JoltPhysics/blob/v5.6.0/Jolt/Physics/Character/CharacterVirtual.cpp#L1796-L1886)):
1. `CancelVelocityTowardsSteepSlopes` on the current velocity, and stores the result as the velocity.
2. `Update`.
3. **Stick to floor:** if it was supported before and is not now, and it did not move up, `StickToFloor(mStickToFloorStepDown)` sweeps down and snaps onto a floor it finds ([L1768-L1794](https://github.com/jrouwe/JoltPhysics/blob/v5.6.0/Jolt/Physics/Character/CharacterVirtual.cpp#L1768-L1794)).
4. **Walk stairs:** if it moved less horizontally than desired and `CanWalkStairs` (supported, pushing into a too-steep contact), `WalkStairs` sweeps up by `mWalkStairsStepUp`, moves forward (towards the most opposing steep contact within 75 degrees, at least `mWalkStairsMinStepForward`), sweeps down by the same amount plus `mWalkStairsStepDownExtra`, retries at `mWalkStairsStepForwardTest` if the landing is too steep, and fails if no height was gained ([L1589-L1766](https://github.com/jrouwe/JoltPhysics/blob/v5.6.0/Jolt/Physics/Character/CharacterVirtual.cpp#L1589-L1766)). A successful walk forces the ground state to `OnGround`.

`ExtendedUpdateSettings` defaults are **Y-axis vectors**: stick down `(0, -0.5, 0)`, step up `(0, 0.4, 0)`, min step forward 0.02, forward test 0.15, cos 75 degrees, extra step down 0. A zero vector turns a feature off ([CharacterVirtual.h L429-L437](https://github.com/jrouwe/JoltPhysics/blob/v5.6.0/Jolt/Physics/Character/CharacterVirtual.h#L429-L437)). The sample re-aims them along `GetUp()` ([CharacterVirtualTest.cpp L68-L77](https://github.com/jrouwe/JoltPhysics/blob/v5.6.0/Samples/Tests/Character/CharacterVirtualTest.cpp#L68-L77)). `CanWalkStairs`, `WalkStairs` and `StickToFloor` are also public on their own ([CharacterVirtual.h L397-L426](https://github.com/jrouwe/JoltPhysics/blob/v5.6.0/Jolt/Physics/Character/CharacterVirtual.h#L397-L426)).

**What each call needs.** Every movement call takes a `BroadPhaseLayerFilter`, `ObjectLayerFilter`, `BodyFilter`, `ShapeFilter` and a `TempAllocator`, which must free everything "by the time this function returns". There is no `JobSystem` parameter, so all the work runs on the calling thread. The samples pass `PhysicsSystem::GetDefault*Filter(layer)`, empty body and shape filters, and the app's temp allocator; the unit tests pass a `TempAllocatorMalloc` ([CharacterVirtualTest.cpp L80-L87](https://github.com/jrouwe/JoltPhysics/blob/v5.6.0/Samples/Tests/Character/CharacterVirtualTest.cpp#L80-L87); [CharacterVirtualTests.cpp L76-L84](https://github.com/jrouwe/JoltPhysics/blob/v5.6.0/UnitTests/Physics/CharacterVirtualTests.cpp#L76-L84)). Each iteration reserves `mMaxNumHits` contacts and twice that many constraints from the allocator ([CharacterVirtual.cpp L1279-L1295](https://github.com/jrouwe/JoltPhysics/blob/v5.6.0/Jolt/Physics/Character/CharacterVirtual.cpp#L1279-L1295)).

**How the host is meant to set velocity** ([CharacterVirtual.h L440-L442](https://github.com/jrouwe/JoltPhysics/blob/v5.6.0/Jolt/Physics/Character/CharacterVirtual.h#L440-L442)):
- `OnGround` and not moving away from the ground: `ground velocity + input + optional jump + dt * gravity`.
- Otherwise: `current vertical velocity + input + dt * gravity`.

The samples do this, call `UpdateGroundVelocity()` first because the platforms may have changed velocity, and keep horizontal velocity in the air when air control is off ([CharacterVirtualTest.cpp L112-L174](https://github.com/jrouwe/JoltPhysics/blob/v5.6.0/Samples/Tests/Character/CharacterVirtualTest.cpp#L112-L174); [CharacterVirtualTests.cpp L44-L71](https://github.com/jrouwe/JoltPhysics/blob/v5.6.0/UnitTests/Physics/CharacterVirtualTests.cpp#L44-L71)). `CancelVelocityTowardsSteepSlopes` can be called by the host before `Update` ([CharacterVirtual.h L370-L374](https://github.com/jrouwe/JoltPhysics/blob/v5.6.0/Jolt/Physics/Character/CharacterVirtual.h#L370-L374)). After a teleport (`SetPosition`), `RefreshContacts` re-finds contacts ([CharacterVirtual.h L453-L454](https://github.com/jrouwe/JoltPhysics/blob/v5.6.0/Jolt/Physics/Character/CharacterVirtual.h#L453-L454)).

**Sliding on gentle slopes is the host's call.** A character standing still on a walkable slope with gravity in its velocity slides down it. The sample stops this in `OnContactSolve` by zeroing the new velocity when the player is not moving, the contact is not moving and the slope is walkable ([CharacterVirtualTest.cpp L386-L395](https://github.com/jrouwe/JoltPhysics/blob/v5.6.0/Samples/Tests/Character/CharacterVirtualTest.cpp#L386-L395)).

**`Character`** is moved by `SetLinearVelocity` / `AddLinearVelocity` / `AddImpulse` on its body before `PhysicsSystem::Update`. Gravity comes from the simulation (times `mGravityFactor`), and contacts change its velocity in the step ([CharacterTest.cpp L92-L107](https://github.com/jrouwe/JoltPhysics/blob/v5.6.0/Samples/Tests/Character/CharacterTest.cpp#L92-L107)).

## 4. Ground state

`EGroundState` ([CharacterBase.h L87-L93](https://github.com/jrouwe/JoltPhysics/blob/v5.6.0/Jolt/Physics/Character/CharacterBase.h#L87-L93)):
- `OnGround`: on walkable ground.
- `OnSteepGround`: on a slope too steep to climb; "the caller should start applying downward velocity if sliding from the slope is desired".
- `NotSupported`: touching something that does not support it (outside the supporting volume); it should fall, and the ground getters describe the touched object.
- `InAir`: touching nothing.

`IsSupported()` is `OnGround || OnSteepGround`. The getters are `GetGroundPosition`, `GetGroundNormal`, `GetGroundVelocity`, `GetGroundMaterial`, `GetGroundBodyID` ("may have been removed!"), `GetGroundSubShapeID` and `GetGroundUserData` ([CharacterBase.h L98-L125](https://github.com/jrouwe/JoltPhysics/blob/v5.6.0/Jolt/Physics/Character/CharacterBase.h#L98-L125)). They are cached values, changed only by the calls below.

**`CharacterVirtual`: `UpdateSupportingContact`** ([CharacterVirtual.cpp L1072-L1250](https://github.com/jrouwe/JoltPhysics/blob/v5.6.0/Jolt/Physics/Character/CharacterVirtual.cpp#L1072-L1250)) runs in `Update`, `RefreshContacts`, `StickToFloor`, `WalkStairs` and a checked `SetShape`:
- Candidates: contacts that collided during the move, plus contacts within `mCollisionTolerance` that it is not moving away from (L1076-L1090).
- Contacts in front of the supporting-volume plane are skipped (L1119). The others count as supported (within the slope limit) or sliding.
- **Ground normal** is the average surface normal of contacts within about 85 degrees of up. **Ground velocity** is the average of their velocities; for a supporting **kinematic** body it is the velocity at the character's position, following the body's rotation over `mLastDeltaTime` (L1136-L1187, L197-L213).
- Ground body, position, material and user data come from the most upward-facing supporting contact, or the deepest contact if none supports (L1169-L1205).
- State: any supported contact gives `OnGround`. Only sliding contacts give `OnSteepGround` if it is moving up relative to the ground; otherwise a trial solve straight down decides between `OnGround` (blocked, as in a funnel) and `OnSteepGround`. Contacts that all fail the supporting volume give `NotSupported`; no contacts give `InAir` (L1207-L1249).

`UpdateGroundVelocity()` re-reads the ground body's velocity without collision detection ([CharacterVirtual.cpp L1496-L1510](https://github.com/jrouwe/JoltPhysics/blob/v5.6.0/Jolt/Physics/Character/CharacterVirtual.cpp#L1496-L1510)). `SetPosition` does not touch the ground state.

**`Character`: `PostSimulation(maxSeparationDistance)`** does one `CollideShape` of its shape at the body's pose. It takes the hit with the most upward normal (first one wins a tie), then sets the state from the supporting volume and slope. Ground velocity is the ground body's point velocity at the contact. Sensors and its own body are ignored ([Character.cpp L78-L89, L129-L215](https://github.com/jrouwe/JoltPhysics/blob/v5.6.0/Jolt/Physics/Character/Character.cpp#L129-L215)). It never reports a funnel as supported. The sample passes its collision tolerance as the separation ([CharacterTest.cpp L46-L50](https://github.com/jrouwe/JoltPhysics/blob/v5.6.0/Samples/Tests/Character/CharacterTest.cpp#L46-L50)).

**Other state.** `GetActiveContacts()` lists the contacts from the last operation; only those with `mHadCollision` are real ([CharacterVirtual.h L509-L511](https://github.com/jrouwe/JoltPhysics/blob/v5.6.0/Jolt/Physics/Character/CharacterVirtual.h#L509-L511)). `HasCollidedWith(BodyID | CharacterID | CharacterVirtual*)` searches it ([L513-L535](https://github.com/jrouwe/JoltPhysics/blob/v5.6.0/Jolt/Physics/Character/CharacterVirtual.h#L513-L535)). `GetMaxHitsExceeded()` flags geometry too complex for `mMaxNumHits` ([L353-L357](https://github.com/jrouwe/JoltPhysics/blob/v5.6.0/Jolt/Physics/Character/CharacterVirtual.h#L353-L357)).

## 5. Contacts

`CharacterContactListener` ([CharacterVirtual.h L155-L214](https://github.com/jrouwe/JoltPhysics/blob/v5.6.0/Jolt/Physics/Character/CharacterVirtual.h#L155-L214)) is set per character (`SetListener`). Every callback has a body version and a `Character...` version for another `CharacterVirtual`:

| Callback | When | What it can change |
|---|---|---|
| `OnAdjustBodyVelocity` | Whenever a body's velocity is read for a contact or the ground | The velocity the character sees (conveyor belts, a ship's "inertial dampener"). "inBody2 is locked during the callback." |
| `OnContactValidate` | Before a contact is used, in the solver, the sweeps and the ground check; may run more than once per contact per update ([CharacterVirtual.cpp L377-L378, L749, L1082](https://github.com/jrouwe/JoltPhysics/blob/v5.6.0/Jolt/Physics/Character/CharacterVirtual.cpp#L377-L378)) | Return false to discard it |
| `OnContactAdded` | First real collision with a body and sub-shape | `CharacterContactSettings`: `mCanPushCharacter`, `mCanReceiveImpulses` (both default true, [L72-L82](https://github.com/jrouwe/JoltPhysics/blob/v5.6.0/Jolt/Physics/Character/CharacterVirtual.h#L72-L82)) |
| `OnContactPersisted` | A contact from the last operation hit again; at most once per contact per tracking window | Same settings |
| `OnContactRemoved` | At the end of the tracking window, for contacts no longer active; gets only `BodyID` and `SubShapeID`, and the body "may have been deleted" | Nothing |
| `OnContactSolve` | Each time the solver slides on a contact | The new character velocity |

A `CharacterContact` carries the position, contact-point velocity, contact normal (towards the character), surface normal (flipped for back faces, replaced by the contact normal when that points more up), distance (above 0 means predictive), fraction, the other body's motion type, whether it is a sensor, its user data, material, the other character, and the `mHadCollision`, `mWasDiscarded`, `mCanPushCharacter` and `mIsBackFacingContact` flags ([CharacterVirtual.h L129-L151](https://github.com/jrouwe/JoltPhysics/blob/v5.6.0/Jolt/Physics/Character/CharacterVirtual.h#L129-L151); filled at [CharacterVirtual.cpp L215-L253](https://github.com/jrouwe/JoltPhysics/blob/v5.6.0/Jolt/Physics/Character/CharacterVirtual.cpp#L215-L253)). For bodies, the user data is the body's user data, which in FenrirPhysics is the `Entity` ([JoltBackend.cpp L1393](../../../packages/FenrirPhysics/src/Backend/Jolt/JoltBackend.cpp)).

**Tracking windows.** `Update`, `WalkStairs`, `StickToFloor` and `ExtendedUpdate` each wrap their work in `StartTrackingContactChanges` / `FinishTrackingContactChanges`. These nest by counter, so one `ExtendedUpdate` gives at most one add or persist and one remove per contact. A host combining calls by hand can wrap them itself ([CharacterVirtual.h L376-L383](https://github.com/jrouwe/JoltPhysics/blob/v5.6.0/Jolt/Physics/Character/CharacterVirtual.h#L376-L383); [CharacterVirtual.cpp L1385-L1440, L495-L528](https://github.com/jrouwe/JoltPhysics/blob/v5.6.0/Jolt/Physics/Character/CharacterVirtual.cpp#L1385-L1440)). The settings from the first callback in a window are reused for later hits in it. With no listener set, nothing is tracked.

**Sensors.** A sensor contact gets `OnContactAdded` and is then discarded, with no further interaction ([CharacterVirtual.cpp L760-L762](https://github.com/jrouwe/JoltPhysics/blob/v5.6.0/Jolt/Physics/Character/CharacterVirtual.cpp#L760-L762); [Architecture.md L721](https://github.com/jrouwe/JoltPhysics/blob/v5.6.0/Docs/Architecture.md?plain=1#L721)). Sweeps skip sensors outright ([L365-L368](https://github.com/jrouwe/JoltPhysics/blob/v5.6.0/Jolt/Physics/Character/CharacterVirtual.cpp#L365-L368)). The character only meets a sensor if its filters let the query reach it (section 9).

**Thread.** Every callback runs synchronously inside the `CharacterVirtual` call, on the thread that made it, because nothing in `CharacterVirtual` dispatches jobs. `OnAdjustBodyVelocity` runs with the body read-locked. `OnContactValidate` from a sweep runs inside a `NarrowPhaseQuery` traversal, which holds the broad phase's shared query lock ([BroadPhaseQuadTree.cpp L556-L561](https://github.com/jrouwe/JoltPhysics/blob/v5.6.0/Jolt/Physics/Collision/BroadPhase/BroadPhaseQuadTree.cpp#L556-L561)), but the body lock is released before the narrow-phase test ([NarrowPhaseQuery.cpp L345-L357](https://github.com/jrouwe/JoltPhysics/blob/v5.6.0/Jolt/Physics/Collision/NarrowPhaseQuery.cpp#L345-L357)). Jolt documents no other restriction on what the callbacks may do. The samples only record and draw. Sample bookkeeping that checks add, persist and remove pair up exactly is at [CharacterVirtualTest.cpp L285-L326](https://github.com/jrouwe/JoltPhysics/blob/v5.6.0/Samples/Tests/Character/CharacterVirtualTest.cpp#L285-L326).

**Pushing dynamic bodies** (`HandleContact`, [CharacterVirtual.cpp L744-L813](https://github.com/jrouwe/JoltPhysics/blob/v5.6.0/Jolt/Physics/Character/CharacterVirtual.cpp#L744-L813)):
- Only `Dynamic` bodies, and only if `mCanReceiveImpulses`.
- It write-locks the body and computes the impulse that would bring the contact point to the character's normal speed (damping 0.9, plus 0.4 of the penetration per `dt`). The impulse is clamped to `mMaxStrength * dt`, and any downward part is removed. It is applied at the contact point immediately, through `AddImpulse`, which also wakes the body ([BodyInterface.cpp L780-L794](https://github.com/jrouwe/JoltPhysics/blob/v5.6.0/Jolt/Physics/Body/BodyInterface.cpp#L780-L794)).
- Separately, standing pushes the ground body down with `mass * g * dt` (section 3); a static or kinematic ground ignores `AddImpulse`.

**Being pushed.** A moving body's contact velocity is the speed of its constraint plane, so it pushes the character during the solve unless `mCanPushCharacter` is false, which zeroes that velocity ([CharacterVirtual.cpp L706-L716, L924-L926](https://github.com/jrouwe/JoltPhysics/blob/v5.6.0/Jolt/Physics/Character/CharacterVirtual.cpp#L706-L716)). Penetration from a body that moved into the character is resolved at `mPenetrationRecoverySpeed`. All of this happens only in the character's own update: between updates, bodies pass into it, and with an inner body they meet a teleported kinematic body instead *(inference)*. The ramp sample covers all four combinations of `mCanPushCharacter` and `mCanReceiveImpulses` ([CharacterVirtualTest.cpp L269-L276](https://github.com/jrouwe/JoltPhysics/blob/v5.6.0/Samples/Tests/Character/CharacterVirtualTest.cpp#L269-L276)).

**`Character`** has no character listener. Its contacts are ordinary body contacts through the `ContactListener` ([CharacterTest.cpp L110-L123](https://github.com/jrouwe/JoltPhysics/blob/v5.6.0/Samples/Tests/Character/CharacterTest.cpp#L110-L123)), and it pushes and is pushed as a dynamic body of its mass.

## 6. Platforms

- **`CharacterVirtual`.** A body's velocity reaches the character in two ways:
  1. **As contact velocity in the solve.** Each contact's `mLinearVelocity` is the body's point velocity, after `OnAdjustBodyVelocity`, so a rising platform pushes the character up and a moving wall pushes it aside ([CharacterVirtual.cpp L178-L195, L222-L223](https://github.com/jrouwe/JoltPhysics/blob/v5.6.0/Jolt/Physics/Character/CharacterVirtual.cpp#L178-L223)).
  2. **As `GetGroundVelocity()`,** which the host must add into the velocity it sets (section 3). The character is **not** carried otherwise. For kinematic ground, the estimate follows the rotation arc rather than the tangent, because "if you just take point velocity * delta time you get an error that accumulates over time" ([CharacterVirtual.h L647-L650](https://github.com/jrouwe/JoltPhysics/blob/v5.6.0/Jolt/Physics/Character/CharacterVirtual.h#L647-L650)).
- **Velocity is read live** from the body's `MotionProperties` at the time of the character call. `MoveKinematic` sets a kinematic body's velocity as soon as it is called ([Body.cpp L81-L94](https://github.com/jrouwe/JoltPhysics/blob/v5.6.0/Jolt/Physics/Body/Body.cpp#L81-L94); [MotionProperties.inl L9-L17](https://github.com/jrouwe/JoltPhysics/blob/v5.6.0/Jolt/Physics/Body/MotionProperties.inl#L9-L17)). FenrirPhysics calls `MoveKinematic` for every kinematic body in its pre-step sync, every tick ([PhysicsWorld.cpp L674-L678](../../../packages/FenrirPhysics/src/PhysicsWorld.cpp)). The samples move platforms first, then set the character's velocity (with `UpdateGroundVelocity`), then update it, all before the step ([CharacterBaseTest.cpp L664-L683, L729-L730](https://github.com/jrouwe/JoltPhysics/blob/v5.6.0/Samples/Tests/Character/CharacterBaseTest.cpp#L664-L683)).
- **Known artefact:** because the character moves at a different time from the bodies, there are "update order artifacts, like the character slightly hovering above an elevator going down" ([Architecture.md L709](https://github.com/jrouwe/JoltPhysics/blob/v5.6.0/Docs/Architecture.md?plain=1#L709)). The unit tests check rotating platforms and platforms moving up ([CharacterVirtualTests.cpp L504-L590](https://github.com/jrouwe/JoltPhysics/blob/v5.6.0/UnitTests/Physics/CharacterVirtualTests.cpp#L504-L590)).
- **Moving frames:** the spaceship sample moves the character with the ship's transform delta each tick and subtracts the ship's velocity in `OnAdjustBodyVelocity` ([CharacterSpaceShipTest.cpp L80-L81, L192-L196](https://github.com/jrouwe/JoltPhysics/blob/v5.6.0/Samples/Tests/Character/CharacterSpaceShipTest.cpp#L80-L81)).
- **`Character`** is carried by friction and contacts in the solver. Its `GetGroundVelocity()` is the ground body's point velocity at the contact, after the step ([Character.cpp L205](https://github.com/jrouwe/JoltPhysics/blob/v5.6.0/Jolt/Physics/Character/Character.cpp#L205)). The sample does not add it to the velocity.

## 7. Character against character

- `CharacterVirtual`s are not registered anywhere, so "it is up to the application to test collision against relevant characters". The hook is an abstract `CharacterVsCharacterCollision` with `CollideCharacter` and `CastCharacter`, set per character ([CharacterVirtual.h L216-L240, L296-L297](https://github.com/jrouwe/JoltPhysics/blob/v5.6.0/Jolt/Physics/Character/CharacterVirtual.h#L216-L240)). Every `CheckCollision` and every sweep calls it after the world query ([CharacterVirtual.cpp L407-L412, L602-L607, L687-L692](https://github.com/jrouwe/JoltPhysics/blob/v5.6.0/Jolt/Physics/Character/CharacterVirtual.cpp#L407-L412)).
- **`CharacterVsCharacterCollisionSimple`** is the shipped version: a flat array with `Add` and `Remove`, an AABB check (or ray-vs-box for casts) against each other character, then a shape-vs-shape test inflated by the other's padding ([CharacterVirtual.cpp L28-L115](https://github.com/jrouwe/JoltPhysics/blob/v5.6.0/Jolt/Physics/Character/CharacterVirtual.cpp#L28-L115)).
- **Cost:** each world query costs O(N) in characters. One `Update` makes up to `mMaxCollisionIterations` (5) collide-plus-cast pairs, and `ExtendedUpdate` adds more for stick-to-floor and stairs. N characters therefore cost O(N²) per tick. Jolt says to use "a hierarchical structure" for many characters ([CharacterVirtual.h L242-L245](https://github.com/jrouwe/JoltPhysics/blob/v5.6.0/Jolt/Physics/Character/CharacterVirtual.h#L242-L245)). The performance test runs 100 characters with inner bodies through the simple version ([CharacterVirtualScene.h L115-L130, L255-L256](https://github.com/jrouwe/JoltPhysics/blob/v5.6.0/PerformanceTest/CharacterVirtualScene.h#L115-L130)).
- **Not thread safe:** "make sure that only one CharacterVirtual is checking collision at a time" ([CharacterVirtual.h L245](https://github.com/jrouwe/JoltPhysics/blob/v5.6.0/Jolt/Physics/Character/CharacterVirtual.h#L245)). It reads other characters' poses while they may be moving.
- **Semantics:** the other character counts as `Kinematic`, with its current linear velocity, the default material and no impulses. Characters "can only be moved in their own update", so A pushing B needs B's `OnCharacterContactAdded` to leave `mCanPushCharacter` true ([CharacterVirtual.cpp L240-L253](https://github.com/jrouwe/JoltPhysics/blob/v5.6.0/Jolt/Physics/Character/CharacterVirtual.cpp#L240-L253); [CharacterVirtual.h L78-L81](https://github.com/jrouwe/JoltPhysics/blob/v5.6.0/Jolt/Physics/Character/CharacterVirtual.h#L78-L81); [CharacterVirtualTest.cpp L328-L341](https://github.com/jrouwe/JoltPhysics/blob/v5.6.0/Samples/Tests/Character/CharacterVirtualTest.cpp#L328-L341)). `mCharacterB` "may be a dangling pointer" when read later through `GetActiveContacts`; use `mCharacterIDB` ([CharacterVirtual.h L144](https://github.com/jrouwe/JoltPhysics/blob/v5.6.0/Jolt/Physics/Character/CharacterVirtual.h#L144)).
- With inner bodies, two characters also meet each other's inner body through the world query. Each character's query skips only its **own** inner body ([CharacterVirtual.cpp L399](https://github.com/jrouwe/JoltPhysics/blob/v5.6.0/Jolt/Physics/Character/CharacterVirtual.cpp#L399)), so unless filtered, B's inner body is a kinematic obstacle to A as well as B's character shape *(inference)*.

## 8. Shape changes

**`CharacterVirtual::SetShape(shape, maxPenetrationDepth, four filters, tempAllocator)`** ([CharacterVirtual.h L460-L469](https://github.com/jrouwe/JoltPhysics/blob/v5.6.0/Jolt/Physics/Character/CharacterVirtual.h#L460-L469); [CharacterVirtual.cpp L1550-L1582](https://github.com/jrouwe/JoltPhysics/blob/v5.6.0/Jolt/Physics/Character/CharacterVirtual.cpp#L1550-L1582)):
- With `maxPenetrationDepth == FLT_MAX`, it switches with no check.
- Otherwise it gathers the new shape's contacts at the **current base position**. If any non-sensor contact penetrates deeper than the limit, it returns `false` and keeps the old shape. If not, it stores those contacts (firing listener callbacks and updating the ground state) and switches.
- Distances there already have the padding subtracted (section 3), so the limit is measured against the padded shape *(inference)*.
- Since the shape's bottom sits at the base (section 2), crouching shrinks the shape from the top and standing grows it upward. The samples make every stance a capsule offset up by half its height ([CharacterBaseTest.cpp L106-L109](https://github.com/jrouwe/JoltPhysics/blob/v5.6.0/Samples/Tests/Character/CharacterBaseTest.cpp#L106-L109)).
- The sample's limit is `1.5 * PhysicsSettings::mPenetrationSlop`, and it then calls `SetInnerBodyShape` ([CharacterVirtualTest.cpp L176-L186](https://github.com/jrouwe/JoltPhysics/blob/v5.6.0/Samples/Tests/Character/CharacterVirtualTest.cpp#L176-L186)). Restoring state uses `FLT_MAX` ([L235](https://github.com/jrouwe/JoltPhysics/blob/v5.6.0/Samples/Tests/Character/CharacterVirtualTest.cpp#L235)).

**`Character::SetShape(shape, maxPenetrationDepth)`** runs the same check through `CollideShape` with zero separation, ignoring sensors and itself, from the body's current transform corrected for the change of centre of mass. It then calls `BodyInterface::SetShape(..., updateMassProperties = false, Activate)` ([Character.cpp L109-L127, L289-L326](https://github.com/jrouwe/JoltPhysics/blob/v5.6.0/Jolt/Physics/Character/Character.cpp#L289-L326)).

**Shape kinds:** `CharacterVirtual`'s padding correction handles convex shapes and `RotatedTranslated`, `Scaled` and `Compound` wrappers. Anything else asserts "Not supported yet!" ([CharacterVirtual.cpp L530-L567](https://github.com/jrouwe/JoltPhysics/blob/v5.6.0/Jolt/Physics/Character/CharacterVirtual.cpp#L530-L567)). Compound character shapes are supported ([ReleaseNotes.md L83](https://github.com/jrouwe/JoltPhysics/blob/v5.6.0/Docs/ReleaseNotes.md?plain=1#L83)). Sweeps cannot cast a mesh ([ShapeCast.h L61](https://github.com/jrouwe/JoltPhysics/blob/v5.6.0/Jolt/Physics/Collision/ShapeCast.h#L61)).

## 9. Filters

- **Which filters.** `Update`, `ExtendedUpdate`, `WalkStairs`, `StickToFloor`, `RefreshContacts`, `SetShape` and `CheckCollision` each take a query-side `BroadPhaseLayerFilter`, `ObjectLayerFilter`, `BodyFilter` and `ShapeFilter`, the same four types `NarrowPhaseQuery` takes. They are passed on unchanged to every `CollideShape` and `CastShape`, with the body filter chained to skip the inner body ([CharacterVirtual.cpp L386-L413, L569-L600](https://github.com/jrouwe/JoltPhysics/blob/v5.6.0/Jolt/Physics/Character/CharacterVirtual.cpp#L386-L413); [NarrowPhaseQuery.h L53, L67](https://github.com/jrouwe/JoltPhysics/blob/v5.6.0/Jolt/Physics/Collision/NarrowPhaseQuery.h#L53)).
- **The character has no object layer of its own,** only the inner body's `mInnerBodyLayer`. What it collides with is whatever these filters say on that call.
- **Query filters run in order:** broad-phase layer, object layer, body, then shape. `GroupFilter` (collision groups) is simulation-only, and queries use `BodyFilter` instead ([Architecture.md L650-L655](https://github.com/jrouwe/JoltPhysics/blob/v5.6.0/Docs/Architecture.md?plain=1#L650-L655)).
- **Jolt's helpers:** `GetDefaultBroadPhaseLayerFilter(layer)` and `GetDefaultLayerFilter(layer)` build query filters from the simulation's `ObjectVsBroadPhaseLayerFilter` and `ObjectLayerPairFilter` for one `ObjectLayer` ([PhysicsSystem.h L213-L216](https://github.com/jrouwe/JoltPhysics/blob/v5.6.0/Jolt/Physics/PhysicsSystem.h#L213-L216)), so a query "as if it were a body in layer X" matches the simulation.
- **`Character`** takes no filters. It uses those defaults for its own `mLayer`, plus a body filter that skips itself and every sensor ([Character.cpp L70-L99](https://github.com/jrouwe/JoltPhysics/blob/v5.6.0/Jolt/Physics/Character/Character.cpp#L70-L99)).

**FenrirPhysics's private layers** ([JoltBackend.cpp L74-L255, L1938-L2006](../../../packages/FenrirPhysics/src/Backend/Jolt/JoltBackend.cpp)):
- **Broad-phase trees:** three, `Static`, `Moving` and `Trigger`.
- **Object layers:** one per distinct `LayerKey{broadPhase, layers, mask}` in use, reference-counted and reused.
- **Simulation pairs:** `ObjectPairs` applies the two-sided layer-and-mask rule.
- **Existing query filters:**
  - `QueryBroadPhaseFilter` skips the `Trigger` tree unless `hitTriggers`.
  - `QueryLayerFilter` is **one-sided**: the body's layers ANDed with the query's mask.
  - `QueryBodyFilter` skips sensors unless `hitTriggers`, and skips one `ignoreEntity` by body user data.

What follows for a character, as options rather than choices:
- **(a) Default filters.** Acquiring a `LayerKey` for the character makes `GetDefault*Filter` give the two-sided rule.
- **(b) The query filters.** The existing query filters give the one-sided rule.
- **Triggers.** Leaving the `Trigger` tree out of the broad-phase filter removes all sensor contacts from the character listener; letting it in yields `OnContactAdded` for triggers (section 5).
- **The inner body.** Its `mInnerBodyLayer` must be an `ObjectLayer` from that same registry. A `CollisionGroup` on it would not affect the character's queries.

## 10. Threading and timing

- **Not during the step.** `PhysicsSystem::Update` locks every body mutex for its whole run ([PhysicsSystem.cpp L236, L672](https://github.com/jrouwe/JoltPhysics/blob/v5.6.0/Jolt/Physics/PhysicsSystem.cpp#L236); [Architecture.md L159](https://github.com/jrouwe/JoltPhysics/blob/v5.6.0/Docs/Architecture.md?plain=1#L159)). `CharacterVirtual` hard-codes the **locking** interfaces: `GetNarrowPhaseQuery()`, `GetBodyLockInterface()` and `GetBodyInterface()` ([CharacterVirtual.cpp L325, L405, L600, L769, L1148, L1481](https://github.com/jrouwe/JoltPhysics/blob/v5.6.0/Jolt/Physics/Character/CharacterVirtual.cpp#L325)). Unlike `Character`, it has no `inLockBodies` switch. So it cannot run beside `PhysicsSystem::Update`, and not from a `PhysicsStepListener` either, which is "called while all body and constraint mutexes are locked" ([PhysicsStepListener.h L29](https://github.com/jrouwe/JoltPhysics/blob/v5.6.0/Jolt/Physics/PhysicsStepListener.h#L29)) *(inference)*. It also changes the world as it runs (impulses, inner-body moves) through those interfaces.
- **Concurrently with each other: yes, by design.** "Multiple CharacterVirtuals can update concurrently, so it is not an issue if the game code is parallelized" ([Architecture.md L709](https://github.com/jrouwe/JoltPhysics/blob/v5.6.0/Docs/Architecture.md?plain=1#L709)). The caveats:
  - The simple character-vs-character checker is not thread safe (section 7).
  - The `TempAllocator` is a stack that relies on job ordering and takes no lock ([TempAllocator.h L11-L14](https://github.com/jrouwe/JoltPhysics/blob/v5.6.0/Jolt/Core/TempAllocator.h#L11-L14)), so concurrent characters need separate allocators *(inference)*.
  - Two characters touching the same dynamic body add impulses in thread order (section 11).
- **Order relative to the step: Jolt shows both.**
  - The samples and the performance test update characters **before** `PhysicsSystem::Update` ([SamplesApp.cpp L2589, L2596](https://github.com/jrouwe/JoltPhysics/blob/v5.6.0/Samples/SamplesApp.cpp#L2589); [PerformanceTest.cpp L383-L386](https://github.com/jrouwe/JoltPhysics/blob/v5.6.0/PerformanceTest/PerformanceTest.cpp#L383-L386)).
  - The unit tests step the world **first**, then the character ([CharacterVirtualTests.cpp L40-L84](https://github.com/jrouwe/JoltPhysics/blob/v5.6.0/UnitTests/Physics/CharacterVirtualTests.cpp#L40-L84)).
  - The design point is that the update "can happen at the appropriate moment in the game code" ([Architecture.md L709](https://github.com/jrouwe/JoltPhysics/blob/v5.6.0/Docs/Architecture.md?plain=1#L709)).
  - Before the step, a character's pushes and the gravity impulse on its ground take effect in that same step; platform velocities set by `MoveKinematic` are already visible (section 6).
- **`Character`:** velocity before the step, `PostSimulation` after it ([Architecture.md L705](https://github.com/jrouwe/JoltPhysics/blob/v5.6.0/Docs/Architecture.md?plain=1#L705)).
- **Time step.** The character's `dt` is independent of the step's collision sub-steps, and it is remembered as `mLastDeltaTime` for ground-velocity estimates ([CharacterVirtual.cpp L1457-L1458](https://github.com/jrouwe/JoltPhysics/blob/v5.6.0/Jolt/Physics/Character/CharacterVirtual.cpp#L1457-L1458)). FenrirPhysics calls `PhysicsSystem::Update(dt, collisionSteps, ...)` from `World::Step` ([JoltBackend.cpp L2257-L2269](../../../packages/FenrirPhysics/src/Backend/Jolt/JoltBackend.cpp)).

## 11. Determinism

What the result depends on:
- **Contact order is made deterministic by sorting.** The broad phase returns hits in a non-deterministic order ([Architecture.md L804-L805](https://github.com/jrouwe/JoltPhysics/blob/v5.6.0/Docs/Architecture.md?plain=1#L804-L805)), so `GetContactsAtPosition` sorts contacts by `(BodyID, CharacterID, SubShapeID)` ([CharacterVirtual.cpp L427-L429](https://github.com/jrouwe/JoltPhysics/blob/v5.6.0/Jolt/Physics/Character/CharacterVirtual.cpp#L427-L429); [CharacterVirtual.h L538-L551](https://github.com/jrouwe/JoltPhysics/blob/v5.6.0/Jolt/Physics/Character/CharacterVirtual.h#L538-L551)). The determinism fix for `GetActiveContacts()` order is in [ReleaseNotes.md L371](https://github.com/jrouwe/JoltPhysics/blob/v5.6.0/Docs/ReleaseNotes.md?plain=1#L371).
  - **Exception:** "this will fail if we exceed mMaxNumHits hits", because the hit reduction before sorting depends on arrival order (L258-L309).
- **Sweeps** keep the smallest fraction, so their result does not depend on order, but ties at an equal fraction go to whichever hit arrives first (L339-L341) *(inference)*.
- **Body IDs** sort the contacts, so they must be the same between runs. The inner body is a body like any other: creating it consumes a `BodyID`, unless `mInnerBodyIDOverride` is given "for a deterministic simulation" ([CharacterVirtual.h L63-L64](https://github.com/jrouwe/JoltPhysics/blob/v5.6.0/Jolt/Physics/Character/CharacterVirtual.h#L63-L64); [ReleaseNotes.md L128](https://github.com/jrouwe/JoltPhysics/blob/v5.6.0/Docs/ReleaseNotes.md?plain=1#L128)).
- **`CharacterID`s** come from a **process-global** atomic counter. They are drawn when a `CharacterVirtualSettings` object is **constructed**, through the member's default initializer, not when a character is. `CharacterID::sSetNextCharacterID()` resets the counter for "a second deterministic run" ([CharacterID.h L73-L92](https://github.com/jrouwe/JoltPhysics/blob/v5.6.0/Jolt/Physics/Character/CharacterID.h#L73-L92); [CharacterVirtual.h L34](https://github.com/jrouwe/JoltPhysics/blob/v5.6.0/Jolt/Physics/Character/CharacterVirtual.h#L34)). The performance test calls it before building its characters ([CharacterVirtualScene.h L115](https://github.com/jrouwe/JoltPhysics/blob/v5.6.0/PerformanceTest/CharacterVirtualScene.h#L115)).
- **Update order between characters matters.**
  - Each character reads other characters' current poses and velocities.
  - Each reads bodies' velocities, which earlier characters may have changed: `AddImpulse` changes velocity at once ([Body.inl L140-L144](https://github.com/jrouwe/JoltPhysics/blob/v5.6.0/Jolt/Physics/Body/Body.inl#L140-L144)).
  - So characters updated in a different order, or concurrently on threads, can give different results *(inference)*.
- **Removal callback order** follows iteration of an `UnorderedMap` keyed by contact hash, not the sorted contact order ([CharacterVirtual.cpp L1430-L1438](https://github.com/jrouwe/JoltPhysics/blob/v5.6.0/Jolt/Physics/Character/CharacterVirtual.cpp#L1430-L1438)). Add and persist callbacks follow the solver's order.
- **Jolt checks it.** The performance test hashes every character's position and every character callback into its determinism hash ([CharacterVirtualScene.h L185-L251](https://github.com/jrouwe/JoltPhysics/blob/v5.6.0/PerformanceTest/CharacterVirtualScene.h#L185-L251)).
- **State save and restore.** `SaveState`/`RestoreState` cover the ground state, pose, velocity, last `dt` and colliding contacts. They do not cover user data, materials or the other character pointers, and they are separate from `PhysicsSystem::SaveState` ([CharacterVirtual.cpp L1888-L1982](https://github.com/jrouwe/JoltPhysics/blob/v5.6.0/Jolt/Physics/Character/CharacterVirtual.cpp#L1888-L1982); [CharacterBase.cpp L36-L57](https://github.com/jrouwe/JoltPhysics/blob/v5.6.0/Jolt/Physics/Character/CharacterBase.cpp#L36-L57)). The shape and up vector are not saved; the samples save them themselves ([CharacterVirtualTest.cpp L212-L242](https://github.com/jrouwe/JoltPhysics/blob/v5.6.0/Samples/Tests/Character/CharacterVirtualTest.cpp#L212-L242); [CharacterPlanetTest.cpp L147-L157](https://github.com/jrouwe/JoltPhysics/blob/v5.6.0/Samples/Tests/Character/CharacterPlanetTest.cpp#L147-L157)).
- **`Character`** is part of the body simulation. Its `PostSimulation` keeps the first of equally upward hits ([Character.cpp L157](https://github.com/jrouwe/JoltPhysics/blob/v5.6.0/Jolt/Physics/Character/Character.cpp#L157)), and narrow-phase result order can vary ([Architecture.md L805](https://github.com/jrouwe/JoltPhysics/blob/v5.6.0/Docs/Architecture.md?plain=1#L805)).

## 12. Motion-test queries

What Jolt offers for "sweep a shape and report a safe position", and for "is this pose free":

- **`NarrowPhaseQuery::CastShape(RShapeCast, ShapeCastSettings, baseOffset, collector, four filters)`** ([NarrowPhaseQuery.h L58-L67](https://github.com/jrouwe/JoltPhysics/blob/v5.6.0/Jolt/Physics/Collision/NarrowPhaseQuery.h#L58-L67)).
  - **The cast.** `RShapeCast` is built from a centre-of-mass transform plus a direction whose length is the cast distance; `sFromWorldTransform` converts from an origin transform. The cast shape "cannot be mesh shape" ([ShapeCast.h L30-L39, L61-L65](https://github.com/jrouwe/JoltPhysics/blob/v5.6.0/Jolt/Physics/Collision/ShapeCast.h#L30-L39)).
  - **The result.** `ShapeCastResult` gives `mFraction` ("CenterOfMassOnHit = Start + value * (End - Start)"), contact points, `mPenetrationAxis` (the normal is its negated, normalized value), `mPenetrationDepth` and `mIsBackFaceHit`. A shape that starts overlapping reports fraction 0, ordered by depth ([ShapeCast.h L116-L173](https://github.com/jrouwe/JoltPhysics/blob/v5.6.0/Jolt/Physics/Collision/ShapeCast.h#L116-L173)).
  - **The settings.**
    - `mExtraConvexRadius` inflates the cast shape, which is how `CharacterVirtual` adds padding.
    - `mBackFaceModeTriangles`, and `mBackFaceModeConvex` ("should we report starting inside an object and moving out?").
    - `mUseShrunkenShapeAndConvexRadius` and `mReturnDeepestPoint`.
    - The shared `mActiveEdgeMode`, `mActiveEdgeMovementDirection`, `mCollisionTolerance` and `mPenetrationTolerance` ([ShapeCast.h L91-L113](https://github.com/jrouwe/JoltPhysics/blob/v5.6.0/Jolt/Physics/Collision/ShapeCast.h#L91-L113); [CollideShape.h L71-L90](https://github.com/jrouwe/JoltPhysics/blob/v5.6.0/Jolt/Physics/Collision/CollideShape.h#L71-L90)).
  - **The hit is a touching pose, not a safe one.** `CharacterVirtual::ValidateMovement` shows how Jolt derives a safe fraction: it casts the unpadded shape with fraction limit `1 + padding / length`, ignores hits at fraction 0 and hits it is moving away from, then pulls the fraction back so the padded shape just touches the inflated face (a GJK cast against that face), or by `padding / length` if that fails, and clamps to 1 ([CharacterVirtual.cpp L569-L660, L336-L384](https://github.com/jrouwe/JoltPhysics/blob/v5.6.0/Jolt/Physics/Character/CharacterVirtual.cpp#L569-L660)). These helpers are **private**.
- **`NarrowPhaseQuery::CollideShape(shape, scale, comTransform, CollideShapeSettings, baseOffset, collector, four filters)`** ([NarrowPhaseQuery.h L42-L53](https://github.com/jrouwe/JoltPhysics/blob/v5.6.0/Jolt/Physics/Collision/NarrowPhaseQuery.h#L42-L53)).
  - **The result.** `CollideShapeResult` gives contact points, `mPenetrationAxis` ("direction to move shape 2 out of collision along the shortest path") and `mPenetrationDepth` ([CollideShape.h L59-L67](https://github.com/jrouwe/JoltPhysics/blob/v5.6.0/Jolt/Physics/Collision/CollideShape.h#L59-L67)).
  - **Near misses.** `mMaxSeparationDistance > 0` also reports shapes up to that distance away, with negative depth ([CollideShape.h L98-L100](https://github.com/jrouwe/JoltPhysics/blob/v5.6.0/Jolt/Physics/Collision/CollideShape.h#L98-L100)). This is the "penetration query" a depenetrate step can use.
  - **Several overlaps** are resolved by iteration; `CharacterVirtual` does this through its plane solver at `mPenetrationRecoverySpeed` (section 3).
  - **`CollideShapeWithInternalEdgeRemoval`** removes ghost hits on mesh internal edges ([NarrowPhaseQuery.h L55-L56](https://github.com/jrouwe/JoltPhysics/blob/v5.6.0/Jolt/Physics/Collision/NarrowPhaseQuery.h#L55-L56)).
- **Others:**
  - `CollidePoint`, and `CollectTransformedShapes(box)`, which gathers the shapes in a box once for repeated local queries ([NarrowPhaseQuery.h L37-L40, L69-L70](https://github.com/jrouwe/JoltPhysics/blob/v5.6.0/Jolt/Physics/Collision/NarrowPhaseQuery.h#L37-L70)).
  - `TransformedShape::CastShape`/`CollideShape` test against one body or character.
  - `CharacterVirtual::CheckCollision` is the public "all contacts for the character at a particular location", with padding and character-vs-character included ([CharacterVirtual.h L477-L490](https://github.com/jrouwe/JoltPhysics/blob/v5.6.0/Jolt/Physics/Character/CharacterVirtual.h#L477-L490)).
- **Collectors** ([CollisionCollectorImpl.h L12-L207](https://github.com/jrouwe/JoltPhysics/blob/v5.6.0/Jolt/Physics/Collision/CollisionCollectorImpl.h#L12-L207); [CollisionCollector.h L84-L90](https://github.com/jrouwe/JoltPhysics/blob/v5.6.0/Jolt/Physics/Collision/CollisionCollector.h#L84-L90)):
  - Ready-made: `AllHit` (with `Sort`), `ClosestHit`, `ClosestHitPerBody` and `AnyHit`.
  - Custom collectors narrow a query with `UpdateEarlyOutFraction` and stop it with `ForceEarlyOut`.
  - Hit order varies, so results must be sorted for determinism ([Architecture.md L805](https://github.com/jrouwe/JoltPhysics/blob/v5.6.0/Docs/Architecture.md?plain=1#L805)).
- **Locking variants:** `GetNarrowPhaseQuery()` locks bodies; `GetNarrowPhaseQueryNoLock()` does not, "use with great care" ([PhysicsSystem.h L124-L125](https://github.com/jrouwe/JoltPhysics/blob/v5.6.0/Jolt/Physics/PhysicsSystem.h#L124-L125)).
- **FenrirPhysics today.** Its `World::CastShape` and `World::CollideShape` use the no-lock query, **default** settings (no extra radius, ignore back faces, no deepest point, no separation distance) and the query filters. The cast reports `fraction * maxDistance` and the contact point on the cast shape; the overlap reports only entity and collider ([JoltBackend.cpp L2045-L2075, L2138-L2175](../../../packages/FenrirPhysics/src/Backend/Jolt/JoltBackend.cpp)).

---

## What this means for FenrirPhysics

Facts the character tickets will rest on. These are not the decisions themselves.

**ECS modelling** (ticket 32):
- Jolt has two characters. **`Character`** is a dynamic rigid body: it would be one more FenrirPhysics body, simulated, stepped and synced like the others. **`CharacterVirtual`** is not a body: an object FenrirPhysics would own, update and save itself, outside `PhysicsSystem`.
- Jolt recommends `CharacterVirtual` for players and `Character` for cheap AI. Only `CharacterVirtual` has slope blocking, stairs, floor sticking, platform following and a character contact listener.
- A `CharacterVirtual` has pose, velocity, up vector, shape and user data as plain members, with position at the **base** of the shape, not its centre of mass. Its shapes must have their bottom at the origin. Its shape kinds are limited to convex shapes and their rotated, translated, scaled and compound wrappers; no mesh shapes.
- Without an inner body it is invisible to rigid bodies, triggers, contact events and FenrirPhysics's existing queries. The **inner body** is an ordinary kinematic body that Jolt creates, adds, teleports and destroys itself. It needs an `ObjectLayer` from FenrirPhysics's layer registry, takes the character's user data (the entity), consumes a `BodyID`, and must have its shape updated by hand on a stance change.
- Live settings: up, max slope, supporting volume, mass, strength, recovery speed, max hits, shape offset, user data and shape. Construction-only: padding, predictive distance, iteration counts, tolerances, back-face mode and the ID.

**Moving per tick** (ticket 33):
- The host sets velocity and applies gravity. `Update` only moves by that velocity and **never changes it**. The gravity argument only pushes on the ground body.
- Jolt's recipe for the velocity is in section 3. Ground velocity must be added by the host. Sliding on walkable slopes must be stopped by the host (`OnContactSolve` in the sample).
- `ExtendedUpdate` adds steep-slope velocity cancelling, stick-to-floor and stair walking. Its defaults are **Y-axis vectors**: 0.5 m stick-down and 0.4 m step-up.
- Every call needs four query filters and a stack temp allocator, and runs on the calling thread.
- It must run **outside `PhysicsSystem::Update`, and not in a step listener**. Before the step (Jolt's samples) or after it (Jolt's unit tests) are both shown. Before the step, its pushes act in that step and the `MoveKinematic` platform velocities of that tick are visible. Characters can update concurrently with each other, given separate temp allocators and no shared character-vs-character checker.

**State and events** (ticket 34):
- Four ground states: on ground, steep, not supported, in air. Ground normal, position, velocity, material, body (may be stale) and user data are cached, refreshed by `Update`, `RefreshContacts`, stairs, stick-to-floor and a checked `SetShape`. A teleport needs `RefreshContacts`.
- The listener gives added, persisted and removed events with at most one per contact per tracking window, plus validate, solve and adjust-velocity hooks.
- All callbacks run **synchronously on the updating thread**, mid-update. Removed events carry only body and sub-shape IDs. Sensors give an add only.
- `CharacterContact` carries the body's user data (the entity), normals, distance, motion type and an `mIsSensorB` flag.

**Interacting with other bodies** (ticket 35):
- Pushing a dynamic body is an immediate `AddImpulse`, clamped by max strength (N) and never downward. Standing applies `mass * g` to the ground. Both wake the body.
- The character is pushed only inside its own update, through contact velocity and penetration recovery. Per-contact flags `mCanPushCharacter` and `mCanReceiveImpulses` are set in the add and persist callbacks.
- Character against character is an application-supplied interface. The shipped version is a brute-force O(N²) list and not thread safe, treats the other character as kinematic, and lets each character be moved only in its own update. Inner bodies of other characters are also hit unless filtered.
- Triggers see a character only through an inner body. The character sees triggers only if its broad-phase filter admits FenrirPhysics's `Trigger` tree.
- Filters are per call. `GetDefault*Filter` on an acquired layer key gives the two-sided simulation rule; FenrirPhysics's query filters give the one-sided query rule.

**Motion tests and shape changes** (ticket 36):
- `SetShape(shape, maxPenetration)` checks the new shape at the current base position and refuses if any non-sensor contact is deeper than the limit (Jolt's sample uses 1.5 times the penetration slop). Otherwise it switches and refreshes contacts. `FLT_MAX` skips the check.
- A sweep-to-safe-position query can be built on `CastShape` (fraction, normal, back faces, initial overlap at fraction 0, `mExtraConvexRadius` for padding). `CharacterVirtual::ValidateMovement` is Jolt's own pattern for backing a hit off by the padding, but it is private.
- A depenetration or "is this pose free" query can be built on `CollideShape` with penetration axis and depth, plus `mMaxSeparationDistance` for near misses. Results arrive in a varying order and must be sorted.
- FenrirPhysics's current `CastShape` and `CollideShape` use default settings and report no penetration depth or safe fraction.

**Determinism** (all of the above):
- Contacts are sorted by `(BodyID, CharacterID, SubShapeID)`, but only up to `mMaxNumHits` hits.
- `CharacterID` comes from a process-global counter drawn at **settings construction**, and must be reset or assigned for repeatable runs. The inner body's `BodyID` can be fixed with `mInnerBodyIDOverride`.
- The order in which characters update (and any concurrency) affects results, through immediate impulses and characters reading each other's poses.
- Character state is outside `PhysicsSystem::SaveState`.

**Surprises worth carrying forward:**
- `Character` is `Dynamic`, not kinematic, despite its doc comment.
- `Update` leaves velocity untouched, so velocity into a ceiling or wall persists until the host changes it.
- `CharacterVirtual` cannot use the non-locking query path at all.
- Removed-contact callbacks come in hash-map order.
