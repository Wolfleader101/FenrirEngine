# Where does animation live: its own package or inside the renderer?

Type: grilling
Status: claimed
Blocked by: 07, 14

## Question

Is skeletal animation its own package (FenrirAnimation?) or part of FenrirRenderer?

- Which side owns skeletons, poses and clips, and which side skinning (the bone-matrix buffer, skinned meshes)?
- What plain data does it read from and give to physics (the simulated ragdoll pose, the bone-to-body mapping) without either depending on the other?
- Does an app without rendering (a server, a headless test) still animate?
- Which foundation decisions it must wait on before its own spec's tickets can graduate.
