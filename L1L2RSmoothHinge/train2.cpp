#include "util.h"
#include "Parser.h"
#include <omp.h>

const double mu = 0.01;
const double C = 100.0;
const double lambda = 5.0;

void search_a_active( double* w, deque<int>& w_act_index, vector<SparseVec*>& Xt, 
		double* alpha,  vector<int>& a_ind_new, int num_select, int N ){
	
	a_ind_new.clear();

	double* Xw = new double[N];
	for(int i=0;i<N;i++)
		Xw[i] = 0.0;
	//compute Xw
	for(deque<int>::iterator it=w_act_index.begin(); it!=w_act_index.end(); it++){
		int j = *it;
		double wj = w[j];
		SparseVec* xj = Xt[j];
		for(SparseVec::iterator it=xj->begin(); it!=xj->end(); it++){
			Xw[it->first] += it->second*wj;
		}
	}
	//sort 1...N according to Xw[1]...Xw[N]
	vector<int> index;
	for(int i=0;i<N;i++)
		index.push_back(i);
	sort(index.begin(), index.end(), ScoreSmaller(Xw));
	
	//pick top indexes
	for(int r=0;r<N && a_ind_new.size()<num_select; r++){
		int i = index[r];
		if( alpha[i] > 0.0 )//already in active set
			continue;
		
		if( Xw[i] < 1.0 ){ //has loss
			a_ind_new.push_back(i);
		}else{
			break;
		}
	}
	
	delete[] Xw;
}

void search_w_active( double* alpha, deque<int>& a_act_index, vector<SparseVec*>& X, double lambda,  
		double* w, vector<int>& w_ind_new, int num_select, int D ){
	
	w_ind_new.clear();

	double* Xta = new double[D];
	for(int j=0;j<D;j++)
		Xta[j] = 0.0;
	
	//compute |Xta|
	for(deque<int>::iterator it=a_act_index.begin(); it!=a_act_index.end(); it++){
		int i = *it;
		double alpha_i = alpha[i];
		SparseVec* xi = X[i];
		for(SparseVec::iterator it=xi->begin(); it!=xi->end(); it++){
			Xta[it->first] += it->second*alpha_i;
		}
	}
	for(int j=0;j<D;j++)
		Xta[j] = fabs( Xta[j] );
	
	//sort 1...D according to |Xta[1]|...|Xta[D]|
	vector<int> index;
	for(int i=0;i<D;i++)
		index.push_back(i);
	sort(index.begin(), index.end(), ScoreLarger(Xta));
	
	//pick top indexes
	for(int r=0;r<D && w_ind_new.size()<num_select; r++){
		int j = index[r];
		if( w[j] != 0.0 )//already in active set
			continue;
		
		if( fabs(Xta[j]) > lambda ){ //has loss
		//if( Xta[j] > 0.0 ){ //has loss
			w_ind_new.push_back(j);
		}else{
			break;
		}
	}
	
	delete[] Xta;
}

double objective(double* w, int D, double* alpha, int N){
	
	double obj = 0.0;
	for(int i=0;i<D;i++)
		obj += w[i]*w[i];
	obj /= 2.0;

	for(int i=0;i<N;i++)
		obj -= alpha[i];

	for(int i=0;i<N;i++){
		obj += mu*alpha[i]*alpha[i]/2.0;
	}

	return obj;
}

int main(int argc, char** argv){

	
	if( argc < 2 ){
		cerr << "Usage: ./train [train file] (modelFile)\n";
		exit(0);
	}
	
	char* trainFile = argv[1];
	char* modelFile;
	if( argc >= 3 )
		modelFile = argv[2];
	else{
		modelFile = "model";
	}
	
	int D;
	vector<SparseVec*> X;
	vector<SparseVec*> Xt;//X transpose
	vector<double> y;
	Parser::parseSVM(trainFile,D, X, y);
	int N = X.size();
	cerr << "N=" << N << endl;
	cerr << "D=" << D << endl;
	
	for(int i=0;i<N;i++){
		SparseVec* xi = X[i];
		double yi = y[i];
		for(SparseVec::iterator it=xi->begin(); it!=xi->end(); it++)
			it->second *= yi;
	}
	transpose( X, N, D, Xt );

	//initialization
	double* v = new double[D];
	double* w = new double[D];
	bool* w_is_act = new bool[D];
	deque<int> w_act_index;
	double* alpha = new double[N];
	deque<int> a_act_index;
	
	for(int i=0;i<D;i++){
		v[i] = 0.0;
		w[i] = 0.0;
		w_is_act[i] = false;
	}
	for(int i=0;i<N;i++)
		alpha[i] = 0.0;
	
	//X_act stores only active features
	vector<SparseVec*> X_act;
	for(int i=0;i<N;i++)
		X_act.push_back(new SparseVec());
	
	//Compute diagonal of XX^T matrix
	double* Qii = new double[N];
	for(int i=0;i<N;i++){
		
		SparseVec* xi = X[i];
		Qii[i] = 0;
		for(SparseVec::iterator it=xi->begin(); it!=xi->end(); it++)
			Qii[i] += it->second*it->second;
		Qii[i] += mu;
	}
	
	//random add some coordinates into active set as initialization
	/*int init_act_w_size = 10;
	int init_act_a_size = 10;
	for(int i=0;i<init_act_a_size;i++)
		a_act_index.push_front(rand()%N);
	for(int j=0;j<init_act_w_size;j++)
		w_act_index.push_front(rand()%D+1);
	
	//maintain X_act for the inital active set
	for(deque<int>::iterator it=w_act_index.begin(); it!=w_act_index.end(); it++){
		int j = *it;
		SparseVec* xj = Xt[j]; // j-th column of X
		for(SparseVec::iterator it2=xj->begin(); it2!=xj->end(); it2++){ // add to X_act
			X_act[it2->first]->insert(make_pair(j, it2->second));
		}
	}*/
	
	
	//Main Loop
	int num_select = 1000;
	int max_iter = 10000;
	vector<int> w_ind_new, a_ind_new;
	double sa_time=0, sw_time=0, updateAct_time=0, buildXact_time=0;
	double overall_time = 0.0;
	for(int iter=0; iter<max_iter; iter++){
		
		//search new active samples and features
		sa_time -= omp_get_wtime();
		search_a_active( w, w_act_index, Xt, alpha,    a_ind_new, num_select, N );
		sa_time += omp_get_wtime();
		
		sw_time -= omp_get_wtime();
		search_w_active( alpha, a_act_index, X, lambda, w,    w_ind_new, num_select, D );
		sw_time += omp_get_wtime();
		
		buildXact_time -= omp_get_wtime();
		for(vector<int>::iterator it=a_ind_new.begin(); it!=a_ind_new.end(); it++)
			a_act_index.push_front(*it);
		for(vector<int>::iterator it=w_ind_new.begin(); it!=w_ind_new.end(); it++){
			w_act_index.push_front(*it);
			w_is_act[*it] = 1;
		}
		//update X_act based on w_ind_new
		for(vector<int>::iterator it=w_ind_new.begin(); it!=w_ind_new.end(); it++){
			int j = *it;
			SparseVec* xj = Xt[j]; // j-th column of X
			for(SparseVec::iterator it2=xj->begin(); it2!=xj->end(); it2++) // add to X_act
				X_act[it2->first]->push_back(make_pair(j, it2->second));
		}
		buildXact_time += omp_get_wtime();
		
		//maintain relation of w_j, v_j and alpha for j in w_ind_new (minimize Lagrangian w.r.t. w_j given alpha)
		updateAct_time -=  omp_get_wtime();
		for(vector<int>::iterator it=w_ind_new.begin(); it!=w_ind_new.end(); it++){
			int j = *it;
			v[j] = 0.0;
			for(SparseVec::iterator it=Xt[j]->begin(); it!=Xt[j]->end(); it++)
				v[j] += alpha[it->first]*it->second;
			w[j] = prox_l1(v[j], lambda);
		}
		
		/*Solve w.r.t. Active Set (w,alpha) using dual coordinate descent:  
		 * min_a ||prox_{lambda}(X'*a)||^2/2 - 1'a  + ||a||^2/2
		 */
		for(int inner=0;inner<20;inner++){
			for(deque<int>::iterator it=a_act_index.begin(); it!=a_act_index.end(); it++){

				int i = *it;
				SparseVec* xi = X_act[i];

				//compute gradient
				double gi = dot(w, xi) - 1.0 + mu*alpha[i];//alpha[i] due to "smooth" hinge loss
				//update
				double new_alpha = min( max( alpha[i] - gi/Qii[i] , 0.0 ) , C);
				//maintain v=X_act'a, w=prox(v); (j \notin act_set need not be maintained)
				double alpha_diff = new_alpha-alpha[i];
				if(  fabs(alpha_diff) > 1e-12 ){

					//for(HashVec::iterator it=xi->begin(); it!=xi->end(); it++){
					for(SparseVec::iterator it=xi->begin(); it!=xi->end(); it++){
						v[it->first] += alpha_diff * it->second;
						w[it->first] = prox_l1( v[it->first], lambda );
					}
					alpha[i] = new_alpha;
				}
			}
		}
		updateAct_time += omp_get_wtime();
		
		buildXact_time -= omp_get_wtime();
		//Shrink active set of alpha (for alpha_i=0)
		deque<int> tmp;
		for(deque<int>::iterator it=a_act_index.begin(); it!=a_act_index.end(); it++)
			if( alpha[*it]  != 0.0 )
				tmp.push_back(*it);
		a_act_index = tmp;
		//Srhink active set of w (for w_j=0)
		/*tmp.clear();
		for(deque<int>::iterator it=w_act_index.begin(); it!=w_act_index.end(); it++){
			if( w[*it] != 0.0 )
				tmp.push_back(*it);
			else{
				//remove from X_act
				//for(int i=0;i<N;i++)
				//	X_act[i]->erase(*it);
				//w_is_act[*it] = 0;
			}	
		}
		w_act_index = tmp;
		*/
		buildXact_time += omp_get_wtime();
		
		overall_time = sa_time + sw_time + buildXact_time + updateAct_time;
		
		//cerr << "i=" << iter << ", |act_a|=" << a_act_index.size() << ", |act_w|=" << w_act_index.size() << ", sa=" << sa_time << ", sw=" << sw_time << ", build=" << buildXact_time << ", update=" << updateAct_time <<   endl;
		if(iter%10==0)
			cerr << "i=" << iter << ", |act_a|=" << a_act_index.size() << ", |act_w|=" << w_act_index.size() << ", obj=" << objective(w,D,alpha,N) << ", time=" << overall_time << ", sa=" << sa_time << ", sw=" << sw_time << ", build=" << buildXact_time << ", update=" << updateAct_time <<   endl; 
		
		random_shuffle(a_act_index.begin(), a_act_index.end());
		if(iter%1==0)
			cerr << "." ;
	}
	cerr << endl;

	cerr << "w_nnz=" << w_act_index.size() << endl;
	cerr << "alpha_nnz=" << a_act_index.size() << endl;
	cerr << "obj=" << objective(w,D,alpha,N) << endl;
	cerr << "train time=" << overall_time << endl;
	
	//output model
	ofstream fout(modelFile);
	fout << D << " " << w_act_index.size() << endl;
	for(int i=0;i<w_act_index.size();i++){
		int j = w_act_index[i];
		if( fabs(w[j]) > 1e-12 )
			fout << j << " " << w[j] << endl;
	}
	fout.close();
}