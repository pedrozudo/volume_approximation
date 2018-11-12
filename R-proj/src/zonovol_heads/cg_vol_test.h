// VolEsti (volume computation and sampling library)

// Copyright (c) 20012-2018 Vissarion Fisikopoulos
// Copyright (c) 2018 Apostolos Chalkis

#ifndef CG_VOL_TEST_H
#define CG_VOL_TEST_H

// Implementation is based on algorithm from paper "A practical volume algorithm",
// Springer-Verlag Berlin Heidelberg and The Mathematical Programming Society 2015
// Ben Cousins, Santosh Vempala
template <class Polytope, class UParameters, class GParameters, class Point, typename NT>
NT params_gaussian_cooling(Polytope &P,
                             GParameters &var,  // constans for volume
                             UParameters &var2,
                             std::pair<Point,NT> InnerBall,
                             NT &HnRSteps,
                             NT &nGaussians,
                             bool steps_only = false) {
    //typedef typename Polytope::MT 	MT;
    typedef typename Polytope::VT 	VT;
    typedef typename UParameters::RNGType RNGType;
    const NT maxNT = 1.79769e+308;
    const NT minNT = -1.79769e+308;
    NT vol;
    bool round = var.round, done;
    bool print = var.verbose;
    bool rand_only = var.rand_only, deltaset = false;
    unsigned int n = var.n, steps;
    unsigned int walk_len = var.walk_steps, m=P.num_of_hyperplanes();
    unsigned int n_threads = var.n_threads, min_index, max_index, index, min_steps;
    NT error = var.error, curr_eps, min_val, max_val, val;
    NT frac = var.frac;
    RNGType &rng = var.rng;
    typedef typename std::vector<NT>::iterator viterator;
    HnRSteps = 0.0;
    nGaussians = 0.0;

    // Consider Chebychev center as an internal point
    Point c=InnerBall.first;
    NT radius=InnerBall.second;
    if (var.ball_walk){
        if(var.delta<0.0){
            var.delta = 4.0 * radius / NT(n);
            var.deltaset = true;
        }
    }

    // rounding of the polytope if round=true
    NT round_value=1;
    if(round){
#ifdef VOLESTI_DEBUG
        if(print) std::cout<<"\nRounding.."<<std::endl;
#endif
        double tstart1 = (double)clock()/(double)CLOCKS_PER_SEC;
        std::pair<NT,NT> res_round = rounding_min_ellipsoid(P,InnerBall,var2);
        double tstop1 = (double)clock()/(double)CLOCKS_PER_SEC;
#ifdef VOLESTI_DEBUG
        if(print) std::cout << "Rounding time = " << tstop1 - tstart1 << std::endl;
#endif
        round_value=res_round.first;
        std::pair<Point,NT> res=P.ComputeInnerBall();
        c=res.first; radius=res.second;
    }

    // Save the radius of the Chebychev ball
    var.che_rad = radius;

    // Move chebychev center to origin and apply the same shifting to the polytope
    VT c_e(n);
    for(unsigned int i=0; i<n; i++){
        c_e(i)=c[i];  // write chebychev center in an eigen vector
    }
    P.shift(c_e);

    // Initialization for the schedule annealing
    std::vector<NT> a_vals;
    NT ratio = var.ratio;
    NT C = var.C;
    unsigned int N = var.N;

    // Computing the sequence of gaussians
#ifdef VOLESTI_DEBUG
    if(print) std::cout<<"\n\nComputing annealing...\n"<<std::endl;
#endif
    double tstart2 = (double)clock()/(double)CLOCKS_PER_SEC;
    get_annealing_schedule(P, radius, ratio, C, frac, N, var, error, a_vals);
    double tstop2 = (double)clock()/(double)CLOCKS_PER_SEC;
#ifdef VOLESTI_DEBUG
    if(print) std::cout<<"All the variances of schedule_annealing computed in = "<<tstop2-tstart2<<" sec"<<std::endl;
#endif

    unsigned int mm = a_vals.size()-1, j=0;
    if(print){
        for (viterator avalIt = a_vals.begin(); avalIt!=a_vals.end(); avalIt++, j++){
            std::cout<<"a_"<<j<<" = "<<*avalIt<<" ";
        }
        std::cout<<"\n"<<std::endl;
    }
    nGaussians = NT(mm);
    if(steps_only) {
        return nGaussians;
    }

    // Initialization for the approximation of the ratios
    std::vector<NT> fn(mm,0), its(mm,0), lamdas(m,0);
    unsigned int W = var.W;
    std::vector<NT> last_W2(W,0);
    vol=std::pow(M_PI/a_vals[0], (NT(n))/2.0)*std::abs(round_value);
    Point p(n); // The origin is in the Chebychev center of the Polytope
    Point p_prev=p;
    unsigned int coord_prev, i=0;
    viterator fnIt = fn.begin(), itsIt = its.begin(), avalsIt = a_vals.begin(), minmaxIt;

#ifdef VOLESTI_DEBUG
    if(print) std::cout<<"volume of the first gaussian = "<<vol<<"\n"<<std::endl;
    if(print) std::cout<<"computing ratios..\n"<<std::endl;
#endif

    // Compute the first point if CDHR is requested
    if(var.coordinate && !var.ball_walk){
        gaussian_first_coord_point(P,p,p_prev,coord_prev,var.walk_steps,*avalsIt,lamdas,var);
    }
    for ( ; fnIt != fn.end(); fnIt++, itsIt++, avalsIt++, i++) { //iterate over the number of ratios
        //initialize convergence test
        curr_eps = error/std::sqrt((NT(mm)));
        done=false;
        min_val = minNT;
        max_val = maxNT;
        min_index = W-1;
        max_index = W-1;
        index = 0;
        min_steps=0;
        std::vector<NT> last_W=last_W2;

        // Set the radius for the ball walk if it is requested
        if (var.ball_walk) {
            if (var.deltaset) {
                var.delta = 4.0 * radius / std::sqrt(std::max(NT(1.0), *avalsIt) * NT(n));
            }
        }

        while(!done || (*itsIt)<min_steps){

            gaussian_next_point(P,p,p_prev,coord_prev,var.walk_steps,*avalsIt,lamdas,var);

            *itsIt = *itsIt + 1.0;
            *fnIt = *fnIt + eval_exp(p,*(avalsIt+1)) / eval_exp(p,*avalsIt);
            val = (*fnIt) / (*itsIt);

            last_W[index] = val;
            if(val<=min_val){
                min_val = val;
                min_index = index;
            }else if(min_index==index){
                minmaxIt = std::min_element(last_W.begin(), last_W.end());
                min_val = *minmaxIt;
                min_index = std::distance(last_W.begin(), minmaxIt);
            }

            if(val>=max_val){
                max_val = val;
                max_index = index;
            }else if(max_index==index){
                minmaxIt = std::max_element(last_W.begin(), last_W.end());
                max_val = *minmaxIt;
                max_index = std::distance(last_W.begin(), minmaxIt);
            }

            if( (max_val-min_val)/max_val<=curr_eps/2.0 ){
                done=true;
            }

            index = index%W+1;

            if(index==W) index=0;
        }
#ifdef VOLESTI_DEBUG
        if(print) std::cout<<"ratio "<<i<<" = "<<(*fnIt) / (*itsIt)<<" N_"<<i<<" = "<<*itsIt<<std::endl;
#endif
        vol = vol*((*fnIt) / (*itsIt));
    }
    // Compute and print total number of steps in verbose mode only

    NT sum_of_steps = 0.0;
    for(viterator it = its.begin(); it != its.end(); ++it) {
        sum_of_steps += *it;
    }
    steps= int(sum_of_steps);
#ifdef VOLESTI_DEBUG
    if (print) {
        std::cout<<"\nTotal number of steps = "<<steps<<"\n"<<std::endl;
    }
#endif
    HnRSteps = sum_of_steps + N * nGaussians;

    return vol;
}

#endif
