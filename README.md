
<!-- README.md is generated from README.Rmd. Please edit that file -->

# gdalraster

<!-- badges: start -->

<!-- badges: end -->

## Overview

gdalraster is an R interface to the Raster API of the Geospatial Data
Abstraction Library ([GDAL](https://gdal.org/)). Calling signatures
resemble those of the native C, C++ and Python APIs provided by the GDAL
project.

Bindings to GDAL are implemented in class
\[`GDALRaster`\]\[GDALRaster-class\] along with several related
stand-alone functions. These support:

  - manual creation of uninitialized raster datasets
  - creation from existing raster as template
  - read/set raster dataset parameters
  - low-level I/O
  - virtual raster (VRT) for virtual subsetting, resampling and kernel
    filtering
  - access to `gdalwarp` utility for reprojection
  - coordinate transformation
  - spatial reference convenience functions

Additional functionality includes:

  - class \[`RunningStats`\]\[RunningStats-class\] calculates mean and
    variance in one pass, and tracks the min, max, sum, and count (i.e.,
    summary statistics on a data stream). The memory usage of a
    RunningStats object is negligible, and input can be intermittent. It
    scales to large datasets for applications such as raster zonal
    statistics.
  - class \[`CmbTable`\]\[CmbTable-class\] identifies and counts unique
    combinations of integer values using a hash table.
  - `combine()` overlays multiple rasters so that a unique ID is
    assigned to each unique combination of input values. Pixel counts
    for each unique combination are obtained, and combination IDs are
    optionally written to an output raster.
  - `calc()` evaluates an R expression for each pixel in a raster layer
    or stack of layers. Individual pixel coordinates are available as
    variables in the R expression, as either x/y in the raster projected
    coordinate system or inverse projected longitude/latitude.

gdalraster is a fast, lightweight and modern R interface to GDAL. It may
be suitable for applications that primarily need low-level raster I/O,
explicit manipulation of VRT format, or prefer native ‘GDAL’-like
calling. The additional functionality is somewhat aimed at thematic data
analysis but may have other utility.

## Installation

Install the development version from GitHub:

``` r
devtools::install_github("USDAForestService/gdalraster")
```
