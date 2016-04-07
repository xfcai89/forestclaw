      subroutine tag4refinement(mx,my,mbc,meqn,
     &      xlower,ylower,dx,dy,blockno,q,
     &      refine_threshold,init_flag, 
     &      tag_patch)
      implicit none

      integer mx,my, mbc, meqn, tag_patch, init_flag
      integer blockno
      double precision refine_threshold
      double precision xlower, ylower, dx, dy
      double precision q(meqn,1-mbc:mx+mbc,1-mbc:my+mbc)

      integer i,j, mq,m
      double precision xc,yc, qmin, qmax
      double precision dq, dqi, dqj

      qmin = 100.d0
      qmax = -100.d0
      tag_patch = 0

c     # Refine based only on first variable in system.
      mq = 1
      do i = 1,mx
         do j = 1,my

            if (init_flag .eq. 1) then
               xc = xlower + (i-0.5)*dx
               yc = ylower + (j-0.5)*dy
               if (abs(xc - 0.5d0) .lt. dx) then
                  tag_patch = 1
                  return
               endif
            else
               qmin = min(q(mq,i,j),qmin)
               qmax = max(q(mq,i,j),qmax)
               if (qmax - qmin .gt. refine_threshold) then
                  tag_patch = 1
                  return
               endif
            endif
         enddo
      enddo

      end

c     # We tag for coarsening if this coarsened patch isn't tagged for refinement
      subroutine tag4coarsening(mx,my,mbc,meqn,
     &      xlower,ylower,dx,dy, blockno,
     &      qcoarsened, tag_patch)
      implicit none

      integer mx,my, mbc, meqn, tag_patch
      double precision xlower, ylower, dx, dy
      double precision qcoarsened(meqn,1-mbc:mx+mbc,1-mbc:my+mbc)

      integer i,j, mq
      double precision qmin, qmax

c     # The difference between this and the true "refinement" above is
c     # that we can't check ghost cells here.  Also, we may make the
c     # coarsening criteria different from the refinement criteria.
c     # Also, we don't check for an init_flag, since it is unlikely that
c     # we would coarsen an initial grid.

      tag_patch = 0
      qmin = 100.d0
      qmax = -100.d0
      mq = 1
      do i = 1,mx
         do j = 1,my
            qmin = min(qcoarsened(mq,i,j),qmin)
            qmax = max(qcoarsened(mq,i,j),qmax)
            if (qmax - qmin .gt. 0.5d0) then
               tag_patch = 1
               return
            endif
         enddo
      enddo

      end
