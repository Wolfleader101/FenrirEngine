// Fenrir/Clusters.glsl: the light clusters' grid (ADR-0049), as Core/LightClusters lays it out: each view's frustum cut
// into 16 x 9 tiles across its viewport, from its NDC's top left, and 24 slices in depth, spaced exponentially from the
// view depth `near` to `far`. builtin:light-clusters lists each cluster's lights and Fenrir/Lighting.glsl finds its
// pixel's.
#ifndef FENRIR_CLUSTERS_GLSL
#define FENRIR_CLUSTERS_GLSL

const uint FenrirClusterColumns = 16u;
const uint FenrirClusterRows = 9u;
const uint FenrirClusterSlices = 24u;
const uint FenrirClusterCount = FenrirClusterColumns * FenrirClusterRows * FenrirClusterSlices;

// The index of the cluster in column `cell.x`, row `cell.y` and slice `cell.z`: column, then row, then slice.
uint FenrirClusterIndex(uvec3 cell)
{
    return cell.x + FenrirClusterColumns * (cell.y + FenrirClusterRows * cell.z);
}

// The slice the view depth `depth` is in, for slices from `near` whose `scale` is 24 / ln(far / near): 0 nearer than
// `near` and the last past `far`.
uint FenrirClusterSliceOf(float depth, float near, float scale)
{
    if (!(depth > near))
    {
        return 0u;
    }
    return uint(min(floor(log(depth / near) * scale), float(FenrirClusterSlices - 1u)));
}

#endif
