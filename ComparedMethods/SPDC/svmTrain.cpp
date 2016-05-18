#include "Parser.h"
#include "Instance.h"
#include <iostream>
#include <vector>
#include <string>
#include <cmath>
#include <omp.h>
#include <assert.h>

using namespace std;

template<class T>
void shuffle( vector<T>& vect ){
	
	int r;
	for(int i=0;i<vect.size()-1;i++){
		r =  (rand() % (vect.size() - i-1))+1+i ;
		swap(vect[i],vect[r]);
	}
}

double dot( double* v, vector<pair<int,double> >& x ){
	
	double sum=0.0;
	for(int i=0;i<x.size();i++){
	
		int index = x[i].first;
		double value = x[i].second;

		sum += v[index]*value;
	}
	
	return sum;
}

// double dot( double* v1, double* v2, int n ){
	
// 	double sum=0.0;
// 	for(int i = 0; i < n; ++i){
// 		sum += v1[i] * v2[i];
// 	}
// 	return sum;
// }

double prox_l1( double v, double lambda ){
	
	if( fabs(v) > lambda ){
		if( v>0.0 )
			return v - lambda;
		else 
			return v + lambda;
	}
	
	return 0.0;
}

// double objective(double* w, int D, double* alpha, int N){
	
// 	double obj = 0.0;
// 	for(int i=0;i<D;i++)
// 		obj += w[i]*w[i];
// 	obj /= 2.0;
	
// 	cout << "primal: " << obj << endl;
// 	double tmp = obj;
	
// 	for(int i=0;i<N;i++)
// 		obj -= alpha[i];

// 	cout << "second: " << obj-tmp << endl;
// 	tmp = obj;
	
// 	for(int i=0;i<N;i++){
// 		obj += alpha[i]*alpha[i]/2.0;
// 	}

// 	cout << "third: " << obj-tmp << endl;
// 	return obj;
// }

double smooth_hinge_loss(double z, double mu) {
	if (z > mu) {
		return 0;
	} else if (z < 1.0 - mu) {
		return 1.0 - z - 0.5*mu;
	} 
	return 0.5 / mu * (1.0 - z) * (1.0 - z);
}

double primal_objective(vector<Instance*>* data, double* w, int D, double* alpha, int N, 
						double mu, double lambda, double lambda_2){
	
	double loss = 0.0;
	for(int i = 0; i < N; i++) {
		SparseVec xi = data->at(i)->xi;
		double yi = data->at(i)->yi;
		loss += smooth_hinge_loss(dot(w, xi) * yi, mu);
	}
	loss /= N;

	double obj_l2 = 0.0;
	for(int i = 0; i < D; i++) {
		obj_l2 +=  w[i]*w[i];
	}
	obj_l2 *= 0.5 * lambda_2;

	double obj_l1 = 0.0;
	for(int i = 0; i < D; i++) {
		obj_l1 += fabs(w[i]);
	}
	obj_l1 *= lambda;

	return loss + obj_l1 + obj_l2;
}

double get_term1(double lambda_2, double tau, int t_diff) {
	// cout << t_diff << endl;
	return 1.0 / pow((1.0 + lambda_2 * tau), t_diff);
}

int get_t_pos(double v_old, double u_old, int from, int to, double lambda, double lambda_2, double tau) {

	double threshold = log(1.0 + lambda_2 * v_old / (u_old + lambda)) / log(1.0 + lambda_2 * tau);
	while (from - to >= threshold) {
		from--;
		// cout << from << endl;
	}
	assert(from >= to);
	// cout << "pos" << t_old << ", " << from << endl;
	// if (from < to) from = to;
	return from;
}

int get_t_neg(double v_old, double u_old, int from, int to, double lambda, double lambda_2, double tau) {

	double threshold = log(1.0 + lambda_2 * v_old / (u_old - lambda)) / log(1.0 + lambda_2 * tau);
	while (from - to >= threshold) {
		from--;
		// cout << from << endl;
	}
	assert(from >= to);
	// cout << "neg" << from << endl;
	// if (from < to) from = to;
	return from;
}

double get_v_recurse(double vj, double u_old, int repeat, 
					 double lambda, double lambda_2, double tau) {

	double threshold = lambda / (lambda_2 + 1.0/tau);
	for (int i = 0; i < repeat; ++i) {
		vj = (1.0 / (1.0 + lambda_2 * tau)) * (vj - tau * u_old);
		vj = prox_l1(vj, threshold);
		// cout << vj << endl;
	}
	return vj;
}

double lazy_update(double v_old, double u_old, int iter, int t_old, 
				   double lambda, double lambda_2, double tau) {
	// cout << "iter" << iter << ", t_old" << t_old << endl;
	double t1;
	// cout << "v_old:" << v_old << endl;
			
	if (v_old == 0.0) {
		t1 = get_term1(lambda_2, tau, iter - t_old - 1);
		if (-u_old > lambda) {
			return t1 * (u_old + lambda) / lambda_2 - (u_old + lambda) / lambda_2;
		} else if (-u_old < -lambda){
			return t1 * (u_old - lambda) / lambda_2 - (u_old - lambda) / lambda_2;
		} else{
			return 0.0;
		}

	} else if (v_old > 0.0) {
		if (-u_old >= lambda) {
			t1 = get_term1(lambda_2, tau, iter - t_old - 1);
		} else {
			int t_pos = get_t_pos(v_old, u_old, iter, t_old + 1, lambda, lambda_2, tau);
			if (t_pos == iter) {
				t1 = get_term1(lambda_2, tau, t_pos - t_old - 1);
			} else {
				double new_v_old = get_v_recurse(v_old, u_old, t_pos - t_old, lambda, lambda_2, tau);
				// cout << "pos: " <<  new_v_old << endl;
				return lazy_update(new_v_old, u_old, iter, t_pos, lambda, lambda_2, tau);
			}
			
		}
		return t1 * (v_old + (u_old + lambda) / lambda_2) - (u_old + lambda) / lambda_2;

	} else {
		if (-u_old <= -lambda) {
			t1 = get_term1(lambda_2, tau, iter - t_old - 1);
			// cout << "t1:" << t1 << endl;
		} else {
			int t_neg = get_t_neg(v_old, u_old, iter, t_old + 1, lambda, lambda_2, tau);
			// cout << "t_neg:" << t_neg << endl;
			if (t_neg == iter) {
				// cout << "t_neg:" << t_neg << endl;
				t1 = get_term1(lambda_2, tau, t_neg - t_old - 1);
			}else {
				double new_v_old = get_v_recurse(v_old, u_old, t_neg - t_old, lambda, lambda_2, tau);
				// cout << "neg: " << new_v_old << endl;
				// cout << v_old << endl;
				return lazy_update(new_v_old, u_old, iter, t_neg, lambda, lambda_2, tau);
			}
		}
		return t1 * (v_old + (u_old - lambda) / lambda_2) - (u_old - lambda) / lambda_2;
	}
}

int nnz(double* v, int size){
	
	int count=0;
	for(int i=0;i<size;i++)
		if( fabs(v[i]) > 1e-12 )
			count++;
	return count;
}

int main(int argc, char** argv){

	if( argc < 1+4 ){
		cerr << "Usage: svmTrain [train file] [lambda] [lambda_2] [mu] (modelFile)\n";
		exit(0);
	}
	
	char* trainFile = argv[1];
	double lambda = atof(argv[2]);
	double lambda_2 = atof(argv[3]);
	double mu = atof(argv[4]);
	char* modelFile;
	if( argc > 1+4 )
		modelFile = argv[5];
	else{
		modelFile = "model";
	}
	
	int D;
	int N;
	vector<Instance*>* data =  Parser::parseSVM(trainFile,D);
	N = data->size();
	cerr << "N=" << N << endl; // # training data points
	cerr << "D=" << D << endl; // dimension

	//initialization
	double* v = new double[D];
	double* v_new = new double[D];
	double* u = new double[D];
	double* x_bar = new double[D];
	double* alpha = new double[N];

	double* last_t = new double[D];

	double R = 0.0;
	for(int i = 0; i < N; i++) {
		SparseVec xi = data->at(i)->xi;
		double norm = 0.0;
		for (int k = 0; k < xi.size(); k++){	
			double value = xi[k].second;
			norm += value * value;
		}
		if (R < sqrt(norm)) {
			R = sqrt(norm);
		}
	}

	double tau = 0.5 / (R * sqrt(N));
	double sigma = 0.5 / R * sqrt(N);
	double theta = 1.0 - 1.0 / (double(N) + R * sqrt(N));

	lambda /= N; // L1 regularization constant
	lambda_2 /= N; // L2 regularization constant

	cout << "tau: " << tau << endl;
	cout << "sigma: " << sigma << endl;
	cout << "theta: " << theta << endl;
	cout << "lambda: " << lambda << endl;
	cout << "lambda_2: " << lambda_2 << endl;
	cout << "mu: " << mu << endl;
	
	for(int i=0;i<D;i++) {
		v[i] = 0;
		u[i] = 0;
		x_bar[i] = 0;
		last_t[i] = -1;
	}
	
	for(int i=0;i<N;i++){
		alpha[i] = 0;
	}
	
	//Main Loop
	vector<int> index;
	for(int i = 0; i < N; i++) {
		index.push_back(i);
	}
	shuffle(index);

	int max_iter = 1000;
	int iter = 0;
	double nnz_v = 0.0;
	double update_time = 0.0;
	int inner_iter = 0;
	cerr.precision(17);
	while(iter < max_iter){
		
		update_time -= omp_get_wtime();
		for(int r = 0; r < N; r++) {
			
			int i = index[r];
			SparseVec xi = data->at(i)->xi;
			double yi = data->at(i)->yi;
			
			// update dual
			double new_alpha = (yi - dot(x_bar, xi) - (alpha[i] / sigma)) / (-1.0 - (1.0/sigma));
			if (yi > 0) {
				new_alpha = min( max( new_alpha, -1.0 ) , 0.0);
			} else {
				new_alpha = min( max( new_alpha, 0.0 ) , 1.0);
			}
			double alpha_diff = new_alpha-alpha[i];
			alpha[i] = new_alpha;

			// update primal
			// for (int j = 0; j < D; ++j) {
			// 	v_new[j] = (v[j] / tau - u[j]) / (lambda_2 + 1.0/tau);
			// }
			double threshold = lambda / (lambda_2 + 1.0/tau);
			for (int k = 0; k < xi.size(); k++){
				
				int idx = xi[k].first;
				double value = xi[k].second;
				if (inner_iter - last_t[idx] > 1) {
					v_new[idx] = lazy_update(v[idx], u[idx], inner_iter, last_t[idx], lambda, lambda_2, tau);
				} else {
					v_new[idx] = v[idx];
				}

				last_t[idx] = inner_iter;	
				// cout << idx << "->" << iter << endl;			
				v_new[idx] = (v_new[idx] / tau - u[idx]) / (lambda_2 + 1.0/tau);
				v_new[idx] -= alpha_diff * value / (lambda_2 + 1.0/tau);
				v_new[idx] = prox_l1(v_new[idx], threshold);
			}

			// maintain u
			for (int k = 0; k < xi.size(); ++k){
				
				int idx = xi[k].first;
				double value = xi[k].second;
				
				u[idx] += alpha_diff * value / N;
			}

			// maintain x_bar
			// for (int j = 0; j < D; ++j) {
			// 	x_bar[j] = v_new[j] + theta * (v_new[j] - v[j]);
			// 	v[j] = v_new[j];
			// }
			for (int k = 0; k < xi.size(); k++){
				
				int idx = xi[k].first;
				x_bar[idx] = v_new[idx] + theta * (v_new[idx] - v[idx]);
				v[idx] = v_new[idx];
			}
			// for (int j = 0; j < D; ++j) {
			// 	cout << v[j] << endl;
			// }
			// cout << endl;
			// for (int k = 0; k < xi.size(); k++){
			// 	int idx = xi[k].first;
			// 	cout << idx << ": " << v_new[idx] << endl;
			// }
			// cout << endl;
			inner_iter++;
		}
		update_time += omp_get_wtime();
		// exit(0);

		if(iter%10==0) {
			nnz_v = nnz(v, D);
			cerr << "iter=" << iter << ", nnz_a=" << nnz(alpha, N) 
			                        << ", nnz_v=" << nnz_v
			                        << ", obj=" << primal_objective(data, v, D, alpha, N, mu, lambda, lambda_2) 
			                        << ", time=" << update_time << endl ;
		}
		shuffle(index);
		iter++;
	}
	cerr << endl;

	//output model
	ofstream fout(modelFile);
	fout << D << " " << nnz_v << endl;
	for(int i=0;i<D;i++)
		if( fabs(v[i]) > 1e-12 )
			fout << i << " " << v[i] << endl;
	fout.close();
}
