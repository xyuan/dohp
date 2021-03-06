#define _XOPEN_SOURCE 600       /* realpath() is in stdlib.h */

#include <dohpfs.h>
#include <dohpvec.h>
#include <dohpstring.h>
#include <dohpviewerdhm.h>
#include "cont.h"

#include <libgen.h>             /* dirname() */

extern dErr VecView_Dohp_FSCont(Vec,dViewer);

/** Get data set and space to write the next element for this FS
* \note caller is responsible for closing both
**/
static dErr dFSGetDHMLink(dFS fs,dViewer viewer,hid_t *indset,hid_t *inspace)
{
  dViewer_DHM *dhm = viewer->data;
  const char  *fsname;
  htri_t       hflg;
  hsize_t dims[1] = {1},maxdims[1] = {H5S_UNLIMITED};
  hid_t        dset,space,h5t_fs;
  herr_t       herr;
  dErr         err;

  dFunctionBegin;
  err = PetscObjectGetName((dObject)fs,&fsname);dCHK(err);
  hflg = H5Lexists(dhm->fsroot,fsname,H5P_DEFAULT);dH5CHK(hflg,H5Lexists);
  if (!hflg) {
    hsize_t chunk[1] = {1};
    hid_t dcpl;
    space = H5Screate_simple(1,dims,maxdims);dH5CHK(space,H5Screate_simple);
    err = dViewerDHMGetFSType(viewer,&h5t_fs);dCHK(err);
    dcpl = H5Pcreate(H5P_DATASET_CREATE);dH5CHK(dcpl,H5Pcreate);
    herr = H5Pset_chunk(dcpl,1,chunk);dH5CHK(herr,H5Pset_chunk);
    dset = H5Dcreate(dhm->fsroot,fsname,h5t_fs,space,H5P_DEFAULT,dcpl,H5P_DEFAULT);dH5CHK(dset,H5Dcreate);
    herr = H5Pclose(dcpl);dH5CHK(herr,H5Pclose);
  } else {
    err = dH5Dopen(dhm->fsroot,fsname,H5P_DEFAULT,&dset);dCHK(err);
    space = H5Dget_space(dset);dH5CHK(space,H5Dget_space);
    herr = H5Sget_simple_extent_dims(space,dims,NULL);dH5CHK(herr,H5Sget_simple_extent_dims);
#if 0                           /* Handle case where it has not already been written in this state */
    herr = H5Sclose(space);dH5CHK(herr,H5Sclose);
    /* Extend by one */
    dims[0]++;
    herr = H5Dset_extent(dset,dims);dH5CHK(herr,H5Dset_extent);
    /* Select the last entry */
    dims[0]--;
#endif
    space = H5Dget_space(dset);dH5CHK(space,H5Dget_space);
    herr = H5Sselect_elements(space,H5S_SELECT_SET,1,dims);dH5CHK(herr,H5Sselect_elements);
  }
  *indset = dset;
  *inspace = space;
  dFunctionReturn(0);
}

dErr dFSView_Cont_DHM(dFS fs,dViewer viewer)
{
  /* dFS_Cont *cont = fs->data; */
  dViewer_DHM *dhm = viewer->data;
  herr_t herr;
  dErr err;

  dFunctionBegin;
  err = dViewerDHMSetUp(viewer);dCHK(err);
  err = dMeshView(fs->mesh,viewer);dCHK(err);
  {
    dht_FS     fs5;
    dht_Field *field5;
    hid_t      h5t_fs,fsdset,fsspace;
    dInt       i,bs;
    PetscMPIInt size;

    err = dViewerDHMGetFSType(viewer,&h5t_fs);dCHK(err);
    err = dMeshGetTagName(fs->mesh,fs->tag.degree,&fs5.degree);dCHK(err);
    err = dMeshGetTagName(fs->mesh,fs->tag.gcoffset,&fs5.global_offset);dCHK(err);
    err = dMeshGetTagName(fs->mesh,fs->tag.partition,&fs5.partition);dCHK(err);
    err = dMeshGetTagName(fs->mesh,fs->tag.orderedsub,&fs5.ordered_subdomain);dCHK(err);
    err = dMeshGetTagName(fs->mesh,fs->tag.bstatus,&fs5.bstatus);dCHK(err);
    err = dViewerDHMGetReferenceMesh(viewer,fs->mesh,&fs5.mesh);dCHK(err);
    fs5.time = dhm->time;
    err = dFSGetBlockSize(fs,&bs);dCHK(err);
    err = PetscObjectStateQuery((PetscObject)fs,&fs5.internal_state);dCHK(err);
    err = MPI_Comm_size(((dObject)fs)->comm,&size);dCHK(err);
    fs5.number_of_subdomains = size;
    err = dFSGetBoundingBox(fs,fs5.boundingbox);dCHK(err);
    fs5.fields.len = bs;
    err = dMallocA(fs5.fields.len,&field5);dCHK(err);
    for (i=0; i<bs; i++) {
      field5[i].name = fs->fieldname[i];
      field5[i].units.dimensions = (char*)dUnitName(fs->fieldunit[i]); /* we only use it as \c const */
      field5[i].units.scale = dUnitDimensionalize(fs->fieldunit[i],1.0);
    }
    fs5.fields.p = field5;
    err = dFSGetDHMLink(fs,viewer,&fsdset,&fsspace);dCHK(err); /* Get location to write this FS */
    herr = H5Dwrite(fsdset,h5t_fs,H5S_ALL,H5S_ALL,H5P_DEFAULT,&fs5);dH5CHK(herr,H5Dwrite);
    err = dFree(field5);dCHK(err);
    err = dFree(fs5.partition);dCHK(err);
    err = dFree(fs5.ordered_subdomain);dCHK(err);
    err = dFree(fs5.bstatus);dCHK(err);
    err = dFree(fs5.global_offset);dCHK(err);
    err = dFree(fs5.degree);dCHK(err);
    herr = H5Dclose(fsdset);dH5CHK(herr,H5Dclose);
    herr = H5Sclose(fsspace);dH5CHK(herr,H5Sclose);
  }

  /* \todo We need a way to identify the active set in MOAB's file if the FS was only defined on a subset. */

  dFunctionReturn(0);
}

static dErr dVecGetHDF5Hyperslab(Vec X,hsize_t gdim[2],hsize_t offset[2],hsize_t count[2])
{
  dInt    m,low,high,bs;
  dErr    err;

  dFunctionBegin;
  err = VecGetSize(X,&m);dCHK(err);
  err = VecGetOwnershipRange(X,&low,&high);dCHK(err);
  err = VecGetBlockSize(X,&bs);dCHK(err);
  gdim[0]   = m/bs;
  gdim[1]   = bs;
  offset[0] = low/bs;
  offset[1] = 0;
  count[0]  = (high-low)/bs;
  count[1]  = bs;
  dFunctionReturn(0);
}


static dErr VecView_Dohp_FSCont_DHM(Vec X,PetscViewer viewer)
{
  dViewer_DHM *dhm = viewer->data;
  const char  *xname;
  dFS          fs;
  Vec          Xclosure;
  dUnit        fieldunit;
  dScalar     *x;
  hid_t        fsdset,fsspace,curstep,dset,fspace,mspace,plist;
  hsize_t      gdim[2],offset[2],count[2];
  herr_t       herr;
  dErr         err;

  dFunctionBegin;
  err = dViewerDHMSetUp(viewer);dCHK(err);
  err = PetscObjectGetName((PetscObject)X,&xname);dCHK(err);
  err = VecDohpGetFS(X,&fs);dCHK(err);
  if (!fs) dERROR(PETSC_COMM_SELF,PETSC_ERR_ARG_WRONG,"Vector not generated from a FS");
  err = dFSGetFieldUnit(fs,0,&fieldunit);dCHK(err);
  err = dFSGetDHMLink(fs,viewer,&fsdset,&fsspace);dCHK(err); /* we are responsible for closing */
  err = dViewerDHMGetStep(viewer,&curstep);dCHK(err);        /* leave curstep open */
  err = dFSView_Cont_DHM(fs,viewer);dCHK(err);
  err = VecDohpGetClosure(X,&Xclosure);dCHK(err);

  err = dVecGetHDF5Hyperslab(Xclosure,gdim,offset,count);dCHK(err);
  fspace = H5Screate_simple(2,gdim,NULL);dH5CHK(fspace,H5Screate_simple);
  dset = H5Dcreate(curstep,xname,dH5T_SCALAR,fspace,H5P_DEFAULT,H5P_DEFAULT,H5P_DEFAULT);dH5CHK(dset,H5Dcreate);
  herr = H5Sselect_hyperslab(fspace,H5S_SELECT_SET,offset,NULL,count,NULL);dH5CHK(herr,H5Sselect_hyperslab);
  mspace = H5Screate_simple(2,count,NULL);dH5CHK(mspace,H5Screate_simple);

  plist = H5Pcreate(H5P_DATASET_XFER);dH5CHK(plist,H5Pcreate);
#if defined dUSE_PARALLEL_HDF5
  herr = H5Pset_dxpl_mpio(plist,H5FD_MPIO_COLLECTIVE);dH5CHK(herr,H5Pset_dxpl_mpio);
#endif

  err = VecGetArray(Xclosure,&x);dCHK(err);
  herr = H5Dwrite(dset,dH5T_SCALAR,mspace,fspace,plist,x);dH5CHK(herr,H5Dwrite);
  err = VecRestoreArray(Xclosure,&x);dCHK(err);
  err = VecDohpRestoreClosure(X,&Xclosure);dCHK(err);

  /* Write attributes on this dataset */
  {
    char fsname[256];
    ssize_t namelen;
    namelen = H5Iget_name(fsdset,fsname,sizeof fsname);dH5CHK(namelen,H5Iget_name);
    if (!namelen) dERROR(PETSC_COMM_SELF,PETSC_ERR_LIB,"Could not get FS path");
    {
      dht_Vec vecatt[1];
      hsize_t dims[1] = {1};
      hid_t aspace,attr,vectype;

      err = dViewerDHMGetVecType(viewer,&vectype);dCHK(err);
      /* Initialize data vecatt[0] */
      herr = H5Rcreate(&vecatt[0].fs,dhm->file,fsname,H5R_DATASET_REGION,fsspace);dH5CHK(herr,H5Rcreate);
      vecatt[0].time = dhm->time;
      vecatt[0].units.dimensions = NULL;
      vecatt[0].units.scale = dUnitDimensionalize(fieldunit,1.0);
      err = PetscObjectStateQuery((PetscObject)X,&vecatt[0].internal_state);dCHK(err);
      /* Write attribute */
      aspace = H5Screate_simple(1,dims,NULL);dH5CHK(aspace,H5Screate_simple);
      attr = H5Acreate(dset,"meta",vectype,aspace,H5P_DEFAULT,H5P_DEFAULT);dH5CHK(attr,H5Acreate);
      herr = H5Awrite(attr,vectype,vecatt);dH5CHK(herr,H5Awrite);
      herr = H5Aclose(attr);dH5CHK(herr,H5Aclose);
      herr = H5Sclose(aspace);dH5CHK(herr,H5Aclose);
    }
  }

  herr = H5Dclose(dset);dH5CHK(herr,H5Dclose);
  herr = H5Pclose(plist);dH5CHK(herr,H5Pclose);
  herr = H5Sclose(fspace);dH5CHK(herr,H5Sclose);
  herr = H5Sclose(mspace);dH5CHK(herr,H5Sclose);
  herr = H5Dclose(fsdset);dH5CHK(herr,H5Dclose);
  herr = H5Sclose(fsspace);dH5CHK(herr,H5Sclose);
  dFunctionReturn(0);
}

dErr VecView_Dohp_FSCont(Vec x,PetscViewer viewer)
{
  dFS fs;
  dBool  isdhm;
  dErr err;

  dFunctionBegin;
  err = VecDohpGetFS(x,&fs);dCHK(err);
  err = PetscTypeCompare((PetscObject)viewer,PETSCVIEWERDHM,&isdhm);dCHK(err);
  if (isdhm) {
    err = VecView_Dohp_FSCont_DHM(x,viewer);dCHK(err);
  } else {
    Vec xclosure;
    err = VecDohpGetClosure(x,&xclosure);dCHK(err);
    err = VecView(xclosure,viewer);dCHK(err);
    err = VecDohpRestoreClosure(x,&xclosure);dCHK(err);
  }
  dFunctionReturn(0);
}

dErr dFSLoadIntoFS_Cont_DHM(PetscViewer viewer,const char name[],dFS fs)
{
  dViewer_DHM *dhm = viewer->data;
  dErr        err;
  hid_t       fsobj,fsspace;
  herr_t      herr;
  dBool       debug = dFALSE;

  dFunctionBegin;
  err = dViewerDHMFindFS(viewer,name,&fsobj,&fsspace);dCHK(err);
  {
    char fsobjname[256];
    ssize_t len;
    hssize_t nrec;
    len = H5Iget_name(fsobj,fsobjname,sizeof fsobjname);dH5CHK(len,H5Iget_name);
    nrec = H5Sget_select_npoints(fsspace);dH5CHK(nrec,H5Sget_select_npoints);
    if (debug) {
      err = dPrintf(PETSC_COMM_SELF,"fsobj name '%s', npoints %zd\n",len?fsobjname:"(no name)",nrec);dCHK(err);
    }
  }
  {
    dht_FS fs5;
    hid_t memspace,h5t_FS,meshobj;
    err = dViewerDHMGetFSType(viewer,&h5t_FS);dCHK(err);
    memspace = H5Screate(H5S_SCALAR);
    herr = H5Dread(fsobj,h5t_FS,memspace,fsspace,H5P_DEFAULT,&fs5);dH5CHK(herr,H5Dread);
    herr = H5Sclose(memspace);

    if (debug) {
      err = dPrintf(PETSC_COMM_SELF,"degree = %s\nglobal_offset = %s\npartition = %s\nordered_subdomain = %s\n",fs5.degree,fs5.global_offset,fs5.partition,fs5.ordered_subdomain);dCHK(err);
    }
    meshobj = H5Rdereference(dhm->meshroot,H5R_OBJECT,&fs5.mesh);dH5CHK(meshobj,H5Rdereference);
    {
      char meshname[dNAME_LEN] = {0};
      ssize_t len;
      len = H5Iget_name(meshobj,meshname,sizeof meshname);dH5CHK(len,H5Iget_name);
      if (debug) {
        err = dPrintf(PETSC_COMM_SELF,"mesh name = %s\n",meshname);dCHK(err);
      }
    }

    {
      hid_t mstring,strspace;
      dMesh mesh;
      char *imeshstr;           /* We are reading to a vlen string so HDF5 will allocate memory */
      char imeshpath[PETSC_MAX_PATH_LEN],tmp[PATH_MAX];
      const char *filepath;
      err = dViewerDHMGetStringTypes(viewer,NULL,&mstring,&strspace);dCHK(err);
      herr = H5Dread(meshobj,mstring,H5S_ALL,H5S_ALL,H5P_DEFAULT,&imeshstr);dH5CHK(herr,H5Dread);
      err = PetscViewerFileGetName(viewer,&filepath);dCHK(err);
      realpath(filepath,tmp);
      err = PetscSNPrintf(imeshpath,sizeof imeshpath,"%s/%s",dirname(tmp),imeshstr);dCHK(err);
      if (debug) {err = dPrintf(PETSC_COMM_SELF,"imeshstr = '%s', imeshpath = '%s'\n",imeshstr,imeshpath);dCHK(err);}
      err = dFSGetMesh(fs,&mesh);dCHK(err);
      err = dMeshSetInFile(mesh,imeshpath,NULL);dCHK(err);
      err = dMeshSetType(mesh,dMESHSERIAL);dCHK(err);
      err = dMeshLoad(mesh);dCHK(err);
      {
        dMeshTag tag;
        dJacobi jac;
        dMeshESH set,*sets;
        dInt nsets,bs;
        dIInt readrank = 0;         /* Hard-code the rank for now */
        err = dMeshGetTag(mesh,fs5.partition,&tag);dCHK(err);
        err = dMeshGetTaggedSet(mesh,tag,&readrank,&set);dCHK(err);
        err = dFSSetMesh(fs,mesh,set);dCHK(err); /* Create all the private sets and tags */
        err = dMeshGetTag(mesh,fs5.ordered_subdomain,&tag);dCHK(err);
        err = dMeshGetTaggedSet(mesh,tag,&readrank,&set);dCHK(err);
        if (set != fs->set.ordered) { /* Replace it */
          err = dMeshSetDestroy(mesh,fs->set.ordered);dCHK(err); // @bug Need reference counting for sets, or need to always copy
          fs->set.ordered = set;
        }

        err = dMeshGetTag(mesh,fs5.bstatus,&tag);dCHK(err);
        err = dMeshGetTaggedSets(mesh,tag,0,NULL,&nsets,&sets);dCHK(err);
        for (dInt i=0; i<nsets; i++) {
          dFSBStatus bstat;
          err = dMeshTagSGetData(mesh,tag,&sets[i],1,&bstat,sizeof bstat,dDATA_BYTE);dCHK(err);
          err = dFSRegisterBoundarySet(fs,sets[i],bstat,NULL,NULL);dCHK(err); /* @todo How to recover the user's constraint function and context? */
        }
        err = dFree(sets);dCHK(err);

        err = dFSSetBlockSize(fs,(dInt)fs5.fields.len);dCHK(err);
        err = dFSGetBlockSize(fs,&bs);dCHK(err);
        for (dInt i=0; i<bs; i++) {
          const dht_Field *field5 = fs5.fields.p;
          err = dFSSetFieldName(fs,i,field5[i].name);dCHK(err);
        }

        err = dMeshGetTag(mesh,fs5.degree,&tag);dCHK(err);
        err = dFSGetJacobi(fs,&jac);dCHK(err);
        err = dFSSetDegree(fs,jac,tag);dCHK(err);
        err = dJacobiSetFromOptions(jac);dCHK(err);

        err = dMeshGetTag(mesh,fs5.global_offset,&tag);dCHK(err);
        if (tag != fs->tag.gcoffset) { /* Replace it */
          err = dMeshTagDestroy(mesh,fs->tag.gcoffset);dCHK(err); // @bug we either need reference counting for tags or need to always copy
          fs->tag.gcoffset= tag;
        }
      }
      herr = H5Dvlen_reclaim(mstring,strspace,H5P_DEFAULT,&imeshstr);dH5CHK(herr,H5Dvlen_reclaim);
    }
    herr = H5Dclose(meshobj);dH5CHK(herr,H5Aclose);
    // \bug herr = H5Dvlen_reclaim(&fs5);
  }
  { // The FS has the layout, ordering, and boundary status tags set so we are ready to build the function space.
    dMesh          mesh;
    dMeshAdjacency meshadj;

    err = dFSGetMesh(fs,&mesh);dCHK(err);
    err = dMeshGetAdjacency(mesh,fs->set.ordered,&meshadj);dCHK(err);
    err = dFSPopulatePartitionedSets_Private(fs,meshadj);dCHK(err);
    err = dFSBuildSpaceWithOrderedSet_Private(fs,meshadj);dCHK(err);
    err = dMeshRestoreAdjacency(mesh,fs->set.ordered,&meshadj);dCHK(err);
  }

  herr = H5Sclose(fsspace);dH5CHK(herr,H5Sclose);
  herr = H5Oclose(fsobj);dH5CHK(herr,H5Oclose);
  dFunctionReturn(0);
}

/* Viewer dispatch sucks, we're just binding statically */
dErr VecDohpLoadIntoVector(PetscViewer viewer,const char fieldname[],Vec X)
{
  dErr    err;
  dBool   match;
  dht_Vec vecmeta;
  hid_t   curstep,dset,memspace,filespace,vattr,vectype;
  hsize_t gdim[2],offset[2],count[2];
  herr_t  herr;
  Vec     Xclosure;
  dScalar *x;
  dReal   scale;
  dFS     fs;
  dUnit   unit;

  dFunctionBegin;
  err = PetscTypeCompare((PetscObject)viewer,PETSCVIEWERDHM,&match);dCHK(err);
  if (!match) dERROR(PETSC_COMM_SELF,PETSC_ERR_ARG_WRONG,"The viewer must be type \"%s\"",PETSCVIEWERDHM);
  err = PetscTypeCompare((PetscObject)X,VECDOHP,&match);dCHK(err);
  if (!match) dERROR(PETSC_COMM_SELF,PETSC_ERR_ARG_WRONG,"Vector must have type \"%s\"",VECDOHP);

  err = dViewerDHMSetUp(viewer);dCHK(err);
  err = dViewerDHMGetStep(viewer,&curstep);dCHK(err);
  err = dViewerDHMGetVecType(viewer,&vectype);dCHK(err);
  err = dH5Dopen(curstep,fieldname,H5P_DEFAULT,&dset);dCHK(err);
  err = dH5Aopen(dset,"meta",H5P_DEFAULT,&vattr);dCHK(err);
  herr = H5Aread(vattr,vectype,&vecmeta);dH5CHK(herr,H5Aread);
  herr = H5Aclose(vattr);dH5CHK(herr,H5Aclose);

  err = VecDohpGetClosure(X,&Xclosure);dCHK(err);

  /* \bug when independently reading a subdomain */
  err = dVecGetHDF5Hyperslab(Xclosure,gdim,offset,count);dCHK(err);
  filespace = H5Dget_space(dset);dH5CHK(filespace,H5Dget_space);
  herr = H5Sselect_hyperslab(filespace,H5S_SELECT_SET,offset,NULL,count,NULL);dH5CHK(herr,H5Sselect_hyperslab);
  memspace = H5Screate_simple(2,count,NULL);dH5CHK(memspace,H5Screate_simple);

  err = VecGetArray(Xclosure,&x);dCHK(err);
  herr = H5Dread(dset,dH5T_SCALAR,memspace,filespace,H5P_DEFAULT,x);dH5CHK(herr,H5Dread);
  err = VecRestoreArray(Xclosure,&x);dCHK(err);

  herr = H5Sclose(memspace);dH5CHK(herr,H5Sclose);
  herr = H5Sclose(filespace);dH5CHK(herr,H5Sclose);
  herr = H5Dclose(dset);dH5CHK(herr,H5Dclose);

  err = VecDohpGetFS(X,&fs);dCHK(err);
  err = dFSGetFieldUnit(fs,0,&unit);dCHK(err);
  scale = dUnitNonDimensionalize(unit,vecmeta.units.scale);
  err = VecScale(Xclosure,scale);dCHK(err);

  err = VecDohpRestoreClosure(X,&Xclosure);dCHK(err);
  dFunctionReturn(0);
}
