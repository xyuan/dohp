/**
* @file   dhm.h
* @author Jed Brown <jed@59A2.org>
* @date   Sat Oct 24 17:20:17 2009
*
* @brief DHM is format for storing Dohp vectors in HDF5 with reference to iMesh.
*
* This is a Multiple Time Multiple Domain format.  A mesh defines the macro-topology, but can be a heavy object to
* duplicate at every time step, and this duplication is clumsy to deal with for certain applications because it's not
* easy to tell when the mesh changes.  The FS defines a function space over the mesh.  There can be multiple function
* spaces using the same mesh.  The parallel layout is defined by a global offset and size for each entity, stored as
* tags on the mesh.  The geometry is defined by a vector in some function space, usually giving locations of nodes (this
* changes every time step in ALE methods).
*
* An outline of the format is
*
* /fs/[ID]/mesh_file_name
*         /layout_tags
*         /layout_tags_of_coordinate_fs
*         /id_of_coordinate_vector
* /times/[ID]/time
*            /[FIELD]/id_of_fs
*                    /vector
*
*/

#if !defined _DHM_H
#define _DHM_H

#include <hdf5.h>

#define dH5CHK(hret,func) if (hret < 0) dERROR(PETSC_ERR_LIB, #func)

#include <private/viewerimpl.h>
#include <dohpviewer.h>
#include <dohpfs.h>

/* for the in-memory representation */
#define dH5T_REAL   H5T_NATIVE_DOUBLE /* dReal */
#define dH5T_SCALAR H5T_NATIVE_DOUBLE /* dScalar */
#define dH5T_INT    H5T_NATIVE_INT    /* dInt */

typedef struct {
  char          *filename;
  PetscFileMode  btype;
  hid_t          file,dohproot;
  hid_t          meshroot,fsroot,steproot,typeroot,curstep;
  dReal          time;
  char          *timeunits;
  dReal          timescale;
  dInt           stepnumber;
  hid_t          h5t_mstring,h5t_fstring,h5s_scalar;
} dViewer_DHM;

extern dErr dViewerDHMSetUp(dViewer);
extern dErr dViewerDHMGetStringTypes(PetscViewer,hid_t *fstring,hid_t *mstring,hid_t *sspace);
extern dErr dViewerDHMGetStep(PetscViewer viewer,hid_t *step);
extern dErr dViewerDHMAttributeStringWrite(PetscViewer viewer,hid_t grp,const char *attname,const char *str);
extern dErr dViewerDHMWriteDimensions(PetscViewer viewer,hid_t grp,const char *name,const char *units,dReal scale);

#endif