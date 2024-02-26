/* Implementation of class GDALVector
   Encapsulates one OGRLayer and its GDALDataset
   Chris Toney <chris.toney at usda.gov> */

#include "gdal.h"
#include "cpl_port.h"
#include "cpl_string.h"
#include "ogrsf_frmts.h"
#include "ogr_srs_api.h"

#include "gdalraster.h"
#include "gdalvector.h"


GDALVector::GDALVector() : 
			dsn_in(""),
			layer_in(""),
			is_sql_in(false),
			open_options_in(Rcpp::CharacterVector::create()),
			hDataset(nullptr),
			eAccess(GA_ReadOnly),
			hLayer(nullptr) {}

GDALVector::GDALVector(Rcpp::CharacterVector dsn, std::string layer) : 
			GDALVector(
				dsn,
				layer,
				true,
				Rcpp::CharacterVector::create()) {}

GDALVector::GDALVector(Rcpp::CharacterVector dsn, std::string layer,
		bool read_only) :
			GDALVector(
				dsn,
				layer,
				read_only,
				Rcpp::CharacterVector::create()) {}

GDALVector::GDALVector(Rcpp::CharacterVector dsn, std::string layer,
		bool read_only, Rcpp::CharacterVector open_options) :
			layer_in(layer),
			open_options_in(open_options),
			hDataset(nullptr),
			eAccess(GA_ReadOnly),
			hLayer(nullptr) {

	dsn_in = Rcpp::as<std::string>(_check_gdal_filename(dsn));
	open(read_only);
}

void GDALVector::open(bool read_only) {
	if (dsn_in == "")
		Rcpp::stop("DSN is not set.");
	
	if (hDataset != nullptr) {
		if (is_sql_in)
			GDALDatasetReleaseResultSet(hDataset, hLayer);
		GDALReleaseDataset(hDataset);
		hDataset = nullptr;
		hLayer = nullptr;
	}
	
	if (read_only)
		eAccess = GA_ReadOnly;
	else
		eAccess = GA_Update;

	std::vector<char *> dsoo(open_options_in.size() + 1);
	if (open_options_in.size() > 0) {
		for (R_xlen_t i = 0; i < open_options_in.size(); ++i) {
			dsoo[i] = (char *) (open_options_in[i]);
		}
	}
	dsoo.push_back(nullptr);

	unsigned int nOpenFlags = GDAL_OF_VECTOR;
	if (read_only)
		nOpenFlags |= GDAL_OF_READONLY;
	else
		nOpenFlags |= GDAL_OF_UPDATE;
	
	hDataset = GDALOpenEx(dsn_in.c_str(), nOpenFlags,
			nullptr, dsoo.data(), nullptr);
	if (hDataset == nullptr)
		Rcpp::stop("Open dataset failed.");
	
	if (STARTS_WITH_CI(layer_in.c_str(), "SELECT ")) {
		is_sql_in = true;
		hLayer = GDALDatasetExecuteSQL(hDataset, layer_in.c_str(),
				nullptr, nullptr);
	}
	else {
		is_sql_in = false;
		hLayer = GDALDatasetGetLayerByName(hDataset, layer_in.c_str());
	}
	if (hLayer == nullptr) {
		GDALReleaseDataset(hDataset);
		Rcpp::stop("Get layer failed.");
	}
	else {
		OGR_L_ResetReading(hLayer);
	}
	
	hFDefn = OGR_L_GetLayerDefn(hLayer);
	if (hFDefn == nullptr) {
		if (is_sql_in)
			GDALDatasetReleaseResultSet(hDataset, hLayer);
		GDALReleaseDataset(hDataset);
		Rcpp::stop("Get layer definition failed.");
	}
}

bool GDALVector::isOpen() const {
	if (hDataset == nullptr)
		return false;
	else
		return true;
}

std::string GDALVector::getDsn() const {
	return dsn_in;
}

Rcpp::CharacterVector GDALVector::getFileList() const {
	_checkAccess(GA_ReadOnly);
	
	char **papszFiles;
	papszFiles = GDALGetFileList(hDataset);
	
	int items = CSLCount(papszFiles);
	if (items > 0) {
		Rcpp::CharacterVector files(items);
		for (int i=0; i < items; ++i) {
			files(i) = papszFiles[i];
		}
		CSLDestroy(papszFiles);
		return files;
	}
	else {
		CSLDestroy(papszFiles);
		return "";	
	}
}

std::string GDALVector::getDriverShortName() const {
	_checkAccess(GA_ReadOnly);
		
	GDALDriverH hDriver = GDALGetDatasetDriver(hDataset);
	return GDALGetDriverShortName(hDriver);
}

std::string GDALVector::getDriverLongName() const {
	_checkAccess(GA_ReadOnly);
		
	GDALDriverH hDriver = GDALGetDatasetDriver(hDataset);
	return GDALGetDriverLongName(hDriver);
}

std::string GDALVector::getName() const {
	_checkAccess(GA_ReadOnly);

	return OGR_L_GetName(hLayer);
}

bool GDALVector::testCapability(std::string capability) const {
	_checkAccess(GA_ReadOnly);
	
	return OGR_L_TestCapability(hLayer, capability.c_str());
}

std::string GDALVector::getFIDColumn() const {
	_checkAccess(GA_ReadOnly);

	return OGR_L_GetFIDColumn(hLayer);
}

std::string GDALVector::getGeomType() const {
	_checkAccess(GA_ReadOnly);
	
	OGRwkbGeometryType eType = OGR_L_GetGeomType(hLayer);
	return OGRGeometryTypeToName(eType);
}

std::string GDALVector::getGeometryColumn() const {
	_checkAccess(GA_ReadOnly);

	return OGR_L_GetGeometryColumn(hLayer);
}

std::string GDALVector::getSpatialRef() const {
	// OGRLayer::GetSpatialRef() as WKT string
	_checkAccess(GA_ReadOnly);

	OGRSpatialReferenceH hSRS = OGR_L_GetSpatialRef(hLayer);
	if (hSRS == nullptr)
		Rcpp::stop("Error: could not obtain SRS.");
	char *pszSRS_WKT = nullptr;
	if (OSRExportToWkt(hSRS, &pszSRS_WKT) != OGRERR_NONE) {
		Rcpp::stop("Error exporting SRS to WKT.");
	}
	std::string srs_wkt(pszSRS_WKT);
	CPLFree(pszSRS_WKT);

	return srs_wkt;
}

Rcpp::NumericVector GDALVector::bbox() {
	// Note: bForce=true in tha call to OGR_L_GetExtent(), so the entire
	// layer may be scanned to compute MBR.
	// see: testCapability("FastGetExtent")
	// Depending on the driver, a spatial filter may/may not be taken into
	// account. So it is safer to call bbox() without setting a spatial filter.
	_checkAccess(GA_ReadOnly);

	OGREnvelope envelope;
	if (OGR_L_GetExtent(hLayer, &envelope, true) != OGRERR_NONE)
		Rcpp::stop("Error: the extent of the layer cannot be determined.");
	
	Rcpp::NumericVector bbox_out = {
		envelope.MinX, envelope.MinY, envelope.MaxX, envelope.MaxY};
	
	return bbox_out;
}

Rcpp::List GDALVector::getLayerDefn() const {
	_checkAccess(GA_ReadOnly);
	
	Rcpp::List list_out = Rcpp::List::create();
	std::string sValue;
	int nValue;
	bool bValue;
	int iField;

	// attribute fields
	for (iField=0; iField < OGR_FD_GetFieldCount(hFDefn); ++iField) {
	
		Rcpp::List list_fld_defn = Rcpp::List::create();
		OGRFieldDefnH hFieldDefn = OGR_FD_GetFieldDefn(hFDefn, iField);
		if (hFieldDefn == nullptr)
			Rcpp::stop("Error: could not obtain field definition.");

		OGRFieldType fld_type = OGR_Fld_GetType(hFieldDefn);
		// TODO: add list types, date, time, binary, etc.
		if (fld_type == OFTInteger) {
			sValue = "OFTInteger";
		}
		else if (fld_type == OFTInteger64) {
			sValue = "OFTInteger64";
		}
		else if (fld_type == OFTReal) {
			sValue = "OFTReal";
		}
		else if (fld_type == OFTString) {
			sValue = "OFTString";
		}
		else {
			sValue = "default (read as OFTString)";
		}
		list_fld_defn.push_back(sValue, "type");
		
		nValue = OGR_Fld_GetWidth(hFieldDefn);
		list_fld_defn.push_back(nValue, "width");
		
		nValue = OGR_Fld_GetPrecision(hFieldDefn);
		list_fld_defn.push_back(nValue, "precision");
		
		bValue = OGR_Fld_IsNullable(hFieldDefn);
		list_fld_defn.push_back(bValue, "is_nullable");

		bValue = OGR_Fld_IsUnique(hFieldDefn);
		list_fld_defn.push_back(bValue, "is_unique");

		if (OGR_Fld_GetDefault(hFieldDefn) != nullptr)
			sValue = std::string(OGR_Fld_GetDefault(hFieldDefn));
		else
			sValue = "";
		list_fld_defn.push_back(sValue, "default");

		bValue = OGR_Fld_IsIgnored(hFieldDefn);
		list_fld_defn.push_back(bValue, "is_ignored");

		bValue = false;
		list_fld_defn.push_back(bValue, "is_geom");

		list_out.push_back(list_fld_defn, OGR_Fld_GetNameRef(hFieldDefn));
	}

	// geometry fields
	for (int i = 0; i < OGR_FD_GetGeomFieldCount(hFDefn); ++i) {
	
		Rcpp::List list_geom_fld_defn = Rcpp::List::create();
		OGRGeomFieldDefnH hGeomFldDefn =
				OGR_FD_GetGeomFieldDefn(hFDefn, i);
		if (hGeomFldDefn == nullptr)
			Rcpp::stop("Error: could not obtain geometry field definition.");
		
		OGRwkbGeometryType eType = OGR_GFld_GetType(hGeomFldDefn);
		sValue = std::string(OGRGeometryTypeToName(eType));
		list_geom_fld_defn.push_back(sValue, "type");
		
		OGRSpatialReferenceH hSRS = OGR_GFld_GetSpatialRef(hGeomFldDefn);
		if (hSRS == nullptr)
			Rcpp::stop("Error: could not obtain geometry SRS.");
		char *pszSRS_WKT = nullptr;
		if (OSRExportToWkt(hSRS, &pszSRS_WKT) != OGRERR_NONE) {
			Rcpp::stop("Error exporting geometry SRS to WKT.");
		}
		sValue = std::string(pszSRS_WKT);
		list_geom_fld_defn.push_back(sValue, "srs");
		
		bValue = OGR_GFld_IsNullable(hGeomFldDefn);
		list_geom_fld_defn.push_back(bValue, "is_nullable");
		
		bValue = OGR_GFld_IsIgnored(hGeomFldDefn);
		list_geom_fld_defn.push_back(bValue, "is_ignored");

		bValue = true;
		list_geom_fld_defn.push_back(bValue, "is_geom");
		
		list_out.push_back(list_geom_fld_defn,
				OGR_GFld_GetNameRef(hGeomFldDefn));
				
		CPLFree(pszSRS_WKT);
	}
	
	return list_out;
}

void GDALVector::setAttributeFilter(std::string query) {
	_checkAccess(GA_ReadOnly);

	const char* query_in = nullptr;
	if (query != "")
		query_in = query.c_str();
		
	if (OGR_L_SetAttributeFilter(hLayer, query_in) != OGRERR_NONE)
		Rcpp::stop("Error setting filter, possibly in the query expression");
}

void GDALVector::setSpatialFilterRect(Rcpp::NumericVector bbox) {
	_checkAccess(GA_ReadOnly);
	
	if (Rcpp::any(Rcpp::is_na(bbox)))
		Rcpp::stop("Error: bbox has one or more NA values.");
		
	OGR_L_SetSpatialFilterRect(hLayer, bbox[0], bbox[1], bbox[2], bbox[3]);
}

void GDALVector::clearSpatialFilter() {
	_checkAccess(GA_ReadOnly);

	OGR_L_SetSpatialFilter(hLayer, nullptr);
}

double GDALVector::getFeatureCount() {
	// OGR_L_GetFeatureCount() returns GIntBig so we return as double to R.
	// GDAL doc: Note that some implementations of this method may alter the
	// read cursor of the layer.
	// see: testCapability("FastFeatureCount")
	_checkAccess(GA_ReadOnly);
	
	return OGR_L_GetFeatureCount(hLayer, true);
}

SEXP GDALVector::getNextFeature() {
	_checkAccess(GA_ReadOnly);

	OGRFeatureH hFeature = OGR_L_GetNextFeature(hLayer);
	
	if (hFeature != nullptr) {
		Rcpp::List list_out = Rcpp::List::create();
		int i;

		double FID = static_cast<double>(OGR_F_GetFID(hFeature));
		list_out.push_back(FID, "FID");
		
		for (i = 0; i < OGR_FD_GetFieldCount(hFDefn); ++i) {
			OGRFieldDefnH hFieldDefn = OGR_FD_GetFieldDefn(hFDefn, i);
			if (hFieldDefn == nullptr)
				Rcpp::stop("Error: could not obtain field definition.");
			
			if (!OGR_F_IsFieldSet(hFeature, i) ||
					OGR_F_IsFieldNull(hFeature, i)) {
				continue;
			}
			
			OGRFieldType fld_type = OGR_Fld_GetType(hFieldDefn);
			if (fld_type == OFTInteger) {
				int value = OGR_F_GetFieldAsInteger(hFeature, i);
				list_out.push_back(value, OGR_Fld_GetNameRef(hFieldDefn));
			}
			else if (fld_type == OFTInteger64) {
				// R does not have native int64 so handled as double for now
				double value = static_cast<double>(
						OGR_F_GetFieldAsInteger64(hFeature, i));
				list_out.push_back(value, OGR_Fld_GetNameRef(hFieldDefn));
			}
			else if (fld_type == OFTReal) {
				double value = OGR_F_GetFieldAsDouble(hFeature, i);
				list_out.push_back(value, OGR_Fld_GetNameRef(hFieldDefn));
			}
			else {
				// TODO: support date, time, binary, etc.
				// read as string for now
				std::string value = OGR_F_GetFieldAsString(hFeature, i);
				list_out.push_back(value, OGR_Fld_GetNameRef(hFieldDefn));
			}
		}

		for (i = 0; i < OGR_F_GetGeomFieldCount(hFeature); ++i) {
			OGRGeometryH hGeometry = OGR_F_GetGeomFieldRef(hFeature, i);
			if (hGeometry == nullptr)
				Rcpp::stop("Error: could not obtain geometry reference.");
			char* pszWKT;
			OGR_G_ExportToWkt(hGeometry, &pszWKT);
			std::string wkt(pszWKT);
			OGRGeomFieldDefnH hGeomFldDefn =
					OGR_F_GetGeomFieldDefnRef(hFeature, i);
			if (hGeomFldDefn == nullptr)
				Rcpp::stop("Error: could not obtain geometry field def.");
			list_out.push_back(wkt, OGR_GFld_GetNameRef(hGeomFldDefn));
			CPLFree(pszWKT);
		}

		return list_out;
	}
	else {
		return R_NilValue;
	}
}

void GDALVector::resetReading() {
	_checkAccess(GA_ReadOnly);
	
	OGR_L_ResetReading(hLayer);
}

void GDALVector::layerIntersection(
		GDALVector method_layer,
		GDALVector result_layer,
		bool quiet,
		Rcpp::Nullable<Rcpp::CharacterVector> options) {

	std::vector<char *> opt_list = {nullptr};
	if (options.isNotNull()) {
		Rcpp::CharacterVector options_in(options);
		opt_list.resize(options_in.size() + 1);
		for (R_xlen_t i = 0; i < options_in.size(); ++i) {
			opt_list[i] = (char *) (options_in[i]);
		}
		opt_list[options_in.size()] = nullptr;
	}
	
	OGRErr err = OGR_L_Intersection(
			hLayer,
			method_layer._getOGRLayerH(),
			result_layer._getOGRLayerH(),
			opt_list.data(),
			quiet ? nullptr : GDALTermProgressR,
			nullptr);
	
	if (err != OGRERR_NONE)
		Rcpp::stop("Error during Intersection, or execution was interrupted.");

}

void GDALVector::layerUnion(
		GDALVector method_layer,
		GDALVector result_layer,
		bool quiet,
		Rcpp::Nullable<Rcpp::CharacterVector> options) {

	std::vector<char *> opt_list = {nullptr};
	if (options.isNotNull()) {
		Rcpp::CharacterVector options_in(options);
		opt_list.resize(options_in.size() + 1);
		for (R_xlen_t i = 0; i < options_in.size(); ++i) {
			opt_list[i] = (char *) (options_in[i]);
		}
		opt_list[options_in.size()] = nullptr;
	}
	
	OGRErr err = OGR_L_Union(
			hLayer,
			method_layer._getOGRLayerH(),
			result_layer._getOGRLayerH(),
			opt_list.data(),
			quiet ? nullptr : GDALTermProgressR,
			nullptr);
	
	if (err != OGRERR_NONE)
		Rcpp::stop("Error during Union, or execution was interrupted.");

}

void GDALVector::layerSymDifference(
		GDALVector method_layer,
		GDALVector result_layer,
		bool quiet,
		Rcpp::Nullable<Rcpp::CharacterVector> options) {

	std::vector<char *> opt_list = {nullptr};
	if (options.isNotNull()) {
		Rcpp::CharacterVector options_in(options);
		opt_list.resize(options_in.size() + 1);
		for (R_xlen_t i = 0; i < options_in.size(); ++i) {
			opt_list[i] = (char *) (options_in[i]);
		}
		opt_list[options_in.size()] = nullptr;
	}
	
	OGRErr err = OGR_L_SymDifference(
			hLayer,
			method_layer._getOGRLayerH(),
			result_layer._getOGRLayerH(),
			opt_list.data(),
			quiet ? nullptr : GDALTermProgressR,
			nullptr);
	
	if (err != OGRERR_NONE)
		Rcpp::stop("Error during SymDifference, or execution was interrupted.");

}

void GDALVector::layerIdentity(
		GDALVector method_layer,
		GDALVector result_layer,
		bool quiet,
		Rcpp::Nullable<Rcpp::CharacterVector> options) {

	std::vector<char *> opt_list = {nullptr};
	if (options.isNotNull()) {
		Rcpp::CharacterVector options_in(options);
		opt_list.resize(options_in.size() + 1);
		for (R_xlen_t i = 0; i < options_in.size(); ++i) {
			opt_list[i] = (char *) (options_in[i]);
		}
		opt_list[options_in.size()] = nullptr;
	}
	
	OGRErr err = OGR_L_Identity(
			hLayer,
			method_layer._getOGRLayerH(),
			result_layer._getOGRLayerH(),
			opt_list.data(),
			quiet ? nullptr : GDALTermProgressR,
			nullptr);
	
	if (err != OGRERR_NONE)
		Rcpp::stop("Error during Identity, or execution was interrupted.");

}

void GDALVector::layerUpdate(
		GDALVector method_layer,
		GDALVector result_layer,
		bool quiet,
		Rcpp::Nullable<Rcpp::CharacterVector> options) {

	std::vector<char *> opt_list = {nullptr};
	if (options.isNotNull()) {
		Rcpp::CharacterVector options_in(options);
		opt_list.resize(options_in.size() + 1);
		for (R_xlen_t i = 0; i < options_in.size(); ++i) {
			opt_list[i] = (char *) (options_in[i]);
		}
		opt_list[options_in.size()] = nullptr;
	}
	
	OGRErr err = OGR_L_Update(
			hLayer,
			method_layer._getOGRLayerH(),
			result_layer._getOGRLayerH(),
			opt_list.data(),
			quiet ? nullptr : GDALTermProgressR,
			nullptr);
	
	if (err != OGRERR_NONE)
		Rcpp::stop("Error during Update, or execution was interrupted.");

}

void GDALVector::layerClip(
		GDALVector method_layer,
		GDALVector result_layer,
		bool quiet,
		Rcpp::Nullable<Rcpp::CharacterVector> options) {

	std::vector<char *> opt_list = {nullptr};
	if (options.isNotNull()) {
		Rcpp::CharacterVector options_in(options);
		opt_list.resize(options_in.size() + 1);
		for (R_xlen_t i = 0; i < options_in.size(); ++i) {
			opt_list[i] = (char *) (options_in[i]);
		}
		opt_list[options_in.size()] = nullptr;
	}
	
	OGRErr err = OGR_L_Clip(
			hLayer,
			method_layer._getOGRLayerH(),
			result_layer._getOGRLayerH(),
			opt_list.data(),
			quiet ? nullptr : GDALTermProgressR,
			nullptr);
	
	if (err != OGRERR_NONE)
		Rcpp::stop("Error during Clip, or execution was interrupted.");

}

void GDALVector::layerErase(
		GDALVector method_layer,
		GDALVector result_layer,
		bool quiet,
		Rcpp::Nullable<Rcpp::CharacterVector> options) {

	std::vector<char *> opt_list = {nullptr};
	if (options.isNotNull()) {
		Rcpp::CharacterVector options_in(options);
		opt_list.resize(options_in.size() + 1);
		for (R_xlen_t i = 0; i < options_in.size(); ++i) {
			opt_list[i] = (char *) (options_in[i]);
		}
		opt_list[options_in.size()] = nullptr;
	}
	
	OGRErr err = OGR_L_Erase(
			hLayer,
			method_layer._getOGRLayerH(),
			result_layer._getOGRLayerH(),
			opt_list.data(),
			quiet ? nullptr : GDALTermProgressR,
			nullptr);
	
	if (err != OGRERR_NONE)
		Rcpp::stop("Error during Erase, or execution was interrupted.");

}

void GDALVector::close() {
	if (hDataset != nullptr) {
		if (is_sql_in)
			GDALDatasetReleaseResultSet(hDataset, hLayer);
		GDALReleaseDataset(hDataset);
		hDataset = nullptr;
		hLayer = nullptr;
	}
}

// ****************************************************************************
// class methods for internal use not exposed in R
// ****************************************************************************

void GDALVector::_checkAccess(GDALAccess access_needed) const {
	if (!isOpen())
		Rcpp::stop("Dataset is not open.");
	
	if (access_needed == GA_Update && eAccess == GA_ReadOnly)
		Rcpp::stop("Dataset is read-only.");
}

OGRLayerH GDALVector::_getOGRLayerH() {
	_checkAccess(GA_ReadOnly);
	
	return hLayer;
}

// ****************************************************************************

RCPP_MODULE(mod_GDALVector) {

    Rcpp::class_<GDALVector>("GDALVector")

    .constructor
    	("Default constructor, only for allocations in std::vector.")
    .constructor<Rcpp::CharacterVector, std::string>
    	("Usage: new(GDALVector, dsn, layer)")
    .constructor<Rcpp::CharacterVector, std::string, bool>
    	("Usage: new(GDALVector, dsn, layer, read_only=[TRUE|FALSE])")
    .constructor<Rcpp::CharacterVector, std::string, bool, Rcpp::CharacterVector>
    	("Usage: new(GDALVector, dsn, layer, read_only, open_options)")
    
    // exposed member functions
    .const_method("getDsn", &GDALVector::getDsn,
    	"Return the DSN.")
    .const_method("isOpen", &GDALVector::isOpen,
    	"Is the dataset open?")
    .method("open", &GDALVector::open,
    	"(Re-)open the dataset on the existing DSN and layer.")
    .const_method("getFileList", &GDALVector::getFileList,
    	"Fetch files forming dataset.")
    .const_method("getDriverShortName", &GDALVector::getDriverShortName,
    	 "Return the short name of the format driver.")
    .const_method("getDriverLongName", &GDALVector::getDriverLongName,
    	"Return the long name of the format driver.")
    .const_method("getName", &GDALVector::getName,
    	"Return the layer name.")
    .const_method("testCapability", &GDALVector::testCapability,
    	"Test if this layer supports the named capability.")
    .const_method("getFIDColumn", &GDALVector::getFIDColumn,
    	"Return name of the underlying db column being used as FID column.")
    .const_method("getGeomType", &GDALVector::getGeomType,
    	"Return the layer geometry type.")
    .const_method("getGeometryColumn", &GDALVector::getGeometryColumn,
    	"Return name of the underlying db column being used as geom column.")
    .const_method("getSpatialRef", &GDALVector::getSpatialRef,
    	"Fetch the spatial reference system for this layer as WKT string.")
    .method("bbox", &GDALVector::bbox,
    	"Return the bounding box (xmin, ymin, xmax, ymax).")
    .const_method("getLayerDefn", &GDALVector::getLayerDefn,
    	"Fetch the schema information for this layer.")
    .method("setAttributeFilter", &GDALVector::setAttributeFilter,
    	"Set a new attribute query.")
    .method("setSpatialFilterRect", &GDALVector::setSpatialFilterRect,
    	"Set a new rectangular spatial filter.")
    .method("clearSpatialFilter", &GDALVector::clearSpatialFilter,
    	"Clear the current spatial filter.")
    .method("getFeatureCount", &GDALVector::getFeatureCount,
    	"Fetch the feature count in this layer.")
    .method("getNextFeature", &GDALVector::getNextFeature,
    	"Fetch the next available feature from this layer.")
    .method("resetReading", &GDALVector::resetReading,
    	"Reset feature reading to start on the first feature.")
    .method("layerIntersection", &GDALVector::layerIntersection,
    	"Intersection of this layer with a method layer.")
    .method("layerUnion", &GDALVector::layerUnion,
    	"Union of this layer with a method layer.")
    .method("layerSymDifference", &GDALVector::layerSymDifference,
    	"Symmetrical difference of this layer and a method layer.")
    .method("layerIdentity", &GDALVector::layerIdentity,
    	"Identify features of this layer with the ones from the method layer.")
    .method("layerUpdate", &GDALVector::layerUpdate,
    	"Update this layer with features from the method layer.")
    .method("layerClip", &GDALVector::layerClip,
    	"Clip off areas that are not covered by the method layer.")
    .method("layerErase", &GDALVector::layerErase,
    	"Remove areas that are covered by the method layer.")
    .method("close", &GDALVector::close,
    	"Release the dataset for proper cleanup.")
    
    ;
}

