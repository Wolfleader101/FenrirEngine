# The project FetchDemoAssets.cmake configures: it fetches MANIFEST's assets into CACHE_DIR, as a demo app does.
cmake_minimum_required(VERSION 3.20)
project(FetchDemoAssetsCase NONE)

include("${MODULE}")
fenrir_fetch_demo_assets(MANIFEST "${MANIFEST}" CACHE_DIR "${CACHE_DIR}")
