%% ADMM for computing gain matrix.  
% --> Speeded up by using sparse matrix representation
% --> Set trace to a fixed value
%
% 
% Inputs:
%
%       - Qs :  Desired formation coordinates (2*n matrix, each column representing coordinate of formation point)
%       - adj:  Graph adjacency matrix (n*n logical matrix)
%
% Outputs:
%
%       - Aopt : Gain matrix
%
%
function Aopt = ADMMGainDesign3D(Qs, adj) 

% print out size information
verbose = 0;

% Gain design for 2D component of the formation 
% (2D component is defined as the projection of the 3D formation on the x-y plane)
Axy = ADMMGainDesign2D(Qs(1:2,:), adj);


%% Gain design for the altitude

% Number of agents
n = size(adj,1);

oneZ = ones(n,1); % Vector of ones
qz = Qs(3,:)'; % Vector of z-coordinates
d = 1; % ambient dimension of problem

% Determine if desired formation is actually 2D
xyform = (std(qz) < 1e-2);

% Set problem dimension
if xyform % Planar formation
    dimkerAz = 1; % Dimension of kernel
    N = oneZ; % Kernel of gain matrix 
else
    dimkerAz = 2; % Dimension of kernel
    N = [qz, oneZ]; % Kernel of gain matrix 
end
m = n - dimkerAz;

% Get orthogonal complement of N
[U,~,~] = svd(N);
% Qbar = U(:,1:dimkerAz);
Q = U(:,(dimkerAz+1):n);
Qt = Q';


%% Preallocate variables

I0 = speye(d*m);
Z0 = sparse(d*m, d*m);
Z = sparse(2*d*m, 2*d*m);

% Cost function's coefficient matrix: f = <C,X>
C = [I0, Z0;
     Z0, Z0];

% Trace of gain matrix must be the specified value in 'trVal'
trVal = d*m; % fixed value for trace


%%%%%%%%%%%%%%%%%%%%%% Find total number of nonzero elements in A and b matrices

% Number of elements in block [X]_11
numElmA1 = (d*m-1)*2 + (d*m)*(d*m-1)/2;
numElmB1 = 0;

% Number of elements in block [X]_12
numElmA2 = d*m + (d*m)*(d*m-1);
numElmB2 = d*m;

% Zero-gain constraints for the given adjacency graph
S = not(adj);
S = S - diag(diag(S));
Su = triu(S); % Upper triangular part
[idxRow, idxCol] = find(Su); % Find location of nonzero entries
% Discard any linear constraint that is trivial (i.e., all zeros)
idxZro = find( sum(abs(Q),2) < 100*eps );
for i = 1 : length(idxZro)
    idxRmv = ( idxRow == idxZro(i) ) | ( idxCol == idxZro(i) );
    idxRow(idxRmv) = [];
    idxCol(idxRmv) = [];
end
numConAdj = length(idxRow); % Number of constraints

% Number of elements in block [X]_22
numElmA3 = numConAdj*(m^2);
numElmB3 = 0;

% Number of elements for trace of [X]_22
numElmA4 = m;
numElmB4 = 1;

% Number of elements for symmetry
numElmA5 = 2* m * (2*m-1);
numElmB5 = 0;

% Number of elements for pinning down the b-vector
numElmA6 = 0;
numElmB6 = 1;

% Total number of elements
numElmAtot = numElmA1 + numElmA2 + numElmA3 + numElmA4 + numElmA5 + numElmA6;
numElmBtot = numElmB1 + numElmB2 + numElmB3 + numElmB4 + numElmB5 + numElmB6;


%%%%%%%%%%%%%%%%%%%%%% Preallocate sparse matrices A & b
%
% Constraint: A * vec(X) = b
%

% Indices of A: entry [Ai(k), Aj(k)] takes value of Av(k) 
% Each row of matrix A will represent a constraint
Ai = zeros(numElmAtot,1);
Aj = zeros(numElmAtot,1);
Av = zeros(numElmAtot,1);

bi = zeros(numElmBtot,1);
bv = zeros(numElmBtot,1);


%% Generate the constraint matrix
%
%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
%%%%%%%%%%%%%%%%%%   Constraints: A . vec(X) = b  %%%%%%%%%%%%%%%%%%%%%%%%%

itrr = 0; % Counter for rows in A constraint
itra = 0; % Counter for entries in A constraint
itrb = 0; % Counter for entries in b constraint


%%%%%%%%%%%%%%%%%%%%%%%%%%%%%% Block [X]_11

% The diagonal entries of X should be equal to the first diagonal entry
for i = 2 : d*m
    itrr = itrr + 1;
    
    itra = itra + 1;    
    idxR = 1;
    idxC = 1;

    Ai(itra) = itrr;
    Aj(itra) = (idxR-1)*(2*d*m) + idxC;
    Av(itra) = 1;
    
    itra = itra + 1;
    idxR = i;
    idxC = i;
    
    Ai(itra) = itrr;
    Aj(itra) = (idxR-1)*(2*d*m) + idxC;
    Av(itra) = -1;
end

% Off-diagonal entries should be zero
for i = 1 : d*m-1
    for j = i+1 : d*m
        itrr = itrr + 1;
        
        itra = itra + 1;
        idxR = i;
        idxC = j;

        Ai(itra) = itrr;
        Aj(itra) = (idxR-1)*(2*d*m) + idxC;
        Av(itra) = 1;
    end
end

if verbose
    fprintf('[X]_11. nnzA = %d (%d). nnzb = %d (%d)\n',...
            nnz(Av), numElmA1, nnz(bv), numElmB1);

    % for next time
    tmpA = nnz(Av);
    tmpb = nnz(bv);
end

%%%%%%%%%%%%%%%%%%%%%%%%%%%%%% Block [X]_12

% Diagonal entries should be 1
for i = 1 : d*m
    itrr = itrr + 1;
    
    itra = itra + 1;
    idxR = i;
    idxC = i + d*m;
    
    Ai(itra) = itrr;
    Aj(itra) = (idxR-1)*(2*d*m) + idxC;
    Av(itra) = 1;
    
    itrb = itrb + 1;
    bi(itrb) = itrr;
    bv(itrb) = 1;
end

% Other entries should be 0
for i = 1 : d*m
    for j = 1 : d*m
        if i ~= j
            itrr = itrr + 1;
            
            itra = itra + 1;
            idxR = i;
            idxC = j + d*m;

            Ai(itra) = itrr;
            Aj(itra) = (idxR-1)*(2*d*m) + idxC;
            Av(itra) = 1;
        end
    end
end

if verbose
    fprintf('[X]_12. nnzA = %d (%d). nnzb = %d (%d)\n',...
            nnz(Av)-tmpA, numElmA2, nnz(bv)-tmpb, numElmB2);

    % for next time
    tmpA = nnz(Av);
    tmpb = nnz(bv);
end

%%%%%%%%%%%%%%%%%%%%%%%%%%%%%% Block [X]_22

% Zero constraints due to the adjacency matrix
for i = 1 : numConAdj
    ii = idxRow(i);
    jj = idxCol(i);
    
    % Term corresponding to row ii and column jj
    QQ = Qt(:,d*(jj-1)+1) * Q(d*(ii-1)+1,:);

    itrr = itrr + 1;
        
    for ki = 1 : d*m
        for kj = 1 : d*m
            itra = itra + 1;
            idxR = d*m + ki;
            idxC = d*m + kj;
            
            Ai(itra) = itrr;
            Aj(itra) = (idxR-1)*(2*d*m) + idxC;
            Av(itra) = QQ(ki, kj);
        end        
    end            
end

if verbose
    fprintf('[X]_22. nnzA = %d (%d). nnzb = %d (%d)\n',...
            nnz(Av)-tmpA, numElmA3, nnz(bv)-tmpb, numElmB3);

    % for next time
    tmpA = nnz(Av);
    tmpb = nnz(bv);
end

% Trace of gain matrix must be the specified value in 'trVal'
itrr = itrr + 1;

for i = 1 : d*m
    itra = itra + 1;
    idxR = d*m + i;
    idxC = idxR;
    
    Ai(itra) = itrr;
    Aj(itra) = (idxR-1)*(2*d*m) + idxC;
    Av(itra) = 1;
end

itrb = itrb + 1;
bi(itrb) = itrr;
bv(itrb) = trVal;

if verbose
    fprintf('trace. nnzA = %d (%d). nnzb = %d (%d)\n',...
            nnz(Av)-tmpA, numElmA4, nnz(bv)-tmpb, numElmB4);

    % for next time
    tmpA = nnz(Av);
    tmpb = nnz(bv);
end

%%%%%%%%%%%%%%%%%%%%%%%%%%%%% Symmetry

for i = 1 : (2*d*m-1)
    for j = (i+1) : 2*d*m
        % Symmetric entries should be equal
        itrr = itrr + 1;
        
        itra = itra + 1;
        idxR = i;
        idxC = j;

        Ai(itra) = itrr;
        Aj(itra) = (idxR-1)*(2*d*m) + idxC;
        Av(itra) = 1;
        
        itra = itra + 1;
        idxR = j;
        idxC = i;

        Ai(itra) = itrr;
        Aj(itra) = (idxR-1)*(2*d*m) + idxC;
        Av(itra) = -1;        
    end
end

if verbose
    fprintf('sym. nnzA = %d (%d). nnzb = %d (%d)\n',...
            nnz(Av)-tmpA, numElmA5, nnz(bv)-tmpb, numElmB5);
end

% Last element set to fix the size of b
itrb = itrb + 1;
bi(itrb) = itrr; 
bv(itrb) = 0;

% % Remove any additional entries
% Ai(itra+1:end) = [];
% Aj(itra+1:end) = [];
% Av(itra+1:end) = [];
%
% bi(itrb+1:end) = [];
% bv(itrb+1:end) = [];


% Make sparse matrices
A = sparse(Ai, Aj, Av);
b = sparse(bi, ones(length(bi), 1), bv);

% Size of optimization variable
sizX = 2 * d*m;



%% ADMM algorithm--full eigendecomposition

As = A.'; % Dual operator
AAs = A * As;
mu = 1; % Penalty    
epsEig = 1e-5;  % Precision for positive eig vals

% Stop criteria
thresh = 1e-4; % Threshold based on change in X updates
threshTr = 10; % Percentage threshold based on the trace value. If trace of X2 reaches within the specified percentage, the algorithm stops.
maxItr = 10; % Maximum # of iterations

% Initialize:
X = [I0 I0; I0 I0];
S = Z;
y = sparse(length(b),1);

for i = 1 : maxItr 
    
    %%%%%%% Update for y
    y = AAs \ (A * reshape(C - S - mu * X, [sizX^2,1]) + mu * b);
    
    %%%%%%% Update for S
    W = C - reshape(As*y, [sizX,sizX]) - mu * X;
    W = (W + W') ./ 2;
    
    [V, D] = eig(full(W));
    dd = diag(D);
    posEig = dd > epsEig;
    S = sparse(real(V(:,posEig) * diag(dd(posEig)) * V(:,posEig).'));
    
    %%%%%%% Update for X
    Xold = X;
    X = (S - W) ./ mu;
    
    %%%%%%% Stop criteria
    difX = sum(abs(Xold(:) - X(:)));
    if difX < thresh % change in X is small
        break 
    end
    
    % trace of sparse matrix for MATLAB Coder
    trPerc = abs(full(sum(diag( X(m+1:end, m+1:end) ))) - trVal) / trVal * 100;
    if trPerc < threshTr % trace of X is close to the desired value
        break
    end
    
end

% Set S=0 to project the final solution and ensure that it satisfies the linear constraints given by the adjacency matrix
S = 0 * S;
y = AAs \ (A * reshape(C - S - mu * X, [sizX^2,1]) + mu * b);
W = C - reshape(As*y, [sizX,sizX]) - mu * X;
W = (W + W.') ./ 2;
X = (S - W) ./ mu;

X = full(X); % Transform X from sparse to full representation 

X2 = X(m+1:end,m+1:end); % The component of X corresponding to the gain matrix
Az = Q * (-X2) * Qt; % The formation gain matrix

% i
% eig(X2)
% trace(X2)


%% 3D formation gain matrix

Aopt = zeros(3*n, 3*n);
for i = 1 : n
    for j = 1 : n
        % Component corresponding to 2D formation
        Aopt(3*i-2:3*i-1, 3*j-2:3*j-1) = Axy(2*i-1:2*i, 2*j-1:2*j);
        % Component corresponding to altitude
        Aopt(3*i, 3*j) = Az(i,j);
    end
end


%% ADMM algorithm--sparse eigendecomposition (use for very large-size problems n>1000)

% As = A'; % Dual operator
% AAs = A * As;
% 
% mu = 1; % Penalty    
% epsEig = 1e-5;  % Precision for positive eig vals
% 
% % Stop criteria
% thresh = 1e-4; % Threshold based on change in X updates
% threshTr = 10; % Percentage threshold based on the trace value. If trace of X2 reaches within the specified percentage, the algorithm stops.
% maxItr = 10; % Maximum # of iterations
% 
% % Initialize:
% X = [I0 I0; I0 I0];
% S = Z;
% y = sparse(length(b),1);
% 
% for i = 1 : maxItr
%     
%     %%%%%%% Update for y
%     y = AAs \ (A * reshape(C - S - mu * X, [sizX^2,1]) + mu * b);
%     
%     
%     %%%%%%% Update for S
%     W = C - reshape(As*y, [sizX,sizX]) - mu * X;
%     W = (W + W')/2;
%     
%     % Find positive eigenvalues of W using an iterative technique
%     V = zeros(sizX);
%     d = zeros(sizX, 1);
%     
%     % Eigenvalues are repeated in paris. We find the corresponding eigvector, 
%     % then find the other eigenvector by rotating the first one 90 degrees
%     [vw1, dw1] = eigs(W,1,'largestreal', 'Tolerance',epsEig, 'FailureTreatment','keep', 'Display', false); 
%     
%     numPos = 0; % Number of positive eigenvalues
%     Wold = W;
%     while dw1 > 10*epsEig %dw(1) > epsEig
%         % Save positive eigenvalues and eigenvectors
%         numPos = numPos + 1;
%         d(numPos) = dw1;
%         V(:, numPos) = vw1;
%         
%         % Remove positive eigenvalue components from matrix W        
%         Wnew = Wold - dw1*(vw1*vw1');
%         
%         % Find most positive eigenvalues of the new W matrix
%         [vw1, dw1] = eigs(Wnew,1,'largestreal', 'Tolerance',epsEig, 'FailureTreatment','keep', 'Display', false); 
%         
%         Wold = Wnew;
%     end
%     
%     S =  V(:,1:numPos) * diag(d(1:numPos)) * V(:,1:numPos).';
%     
%     %%%%%%% Update for X
%     Xold = X;
%     X = sparse((S - W) / mu);
%     
% 
%     %%%%%%% Stop criteria
%     difX = sum(abs(Xold(:) - X(:)));
%     if difX < thresh % change in X is small
%         break 
%     end
%     
%     trPerc = abs(trace( X(m+1:end, m+1:end) ) - trVal) / trVal * 100;
%     if trPerc < threshTr % trace of X is close to the desired value
%         break
%     end
%     
% end
% 
% % Set S=0 to project the final solution and ensure that it satisfies the linear constraints given by the adjacency matrix
% S = 0 * S;
% y = AAs \ (A * reshape(C - S - mu * X, [sizX^2,1]) + mu * b);
% W = C - reshape(As*y, [sizX,sizX]) - mu * X;
% W = (W + W') / 2;
% X = (S - W) / mu;
% 
% X = full(X); % Transform X from sparse to full representation 
% 
% X2 = X(m+1:end, m+1:end); % The componenet of X corresponding to the gain matrix
% Az = Q * (-X2) * Qt; % The formation gain matrix

% i
% eig(X2)
% trace(X2)


%% Test solution

% eig(X2)
% eig(Az)

% % Enforce zero-gain constraint for non-neighbor agents
% Atrim = Az;
% for i = 1 : n
%     for j = 1 : n
%         if (i~=j) && ~adj(i,j)
%             Atrim(2*i-1:2*i, 2*j-1:2*j) = zeros(2);            
%         end
%     end
% end
% 
% eig(Atrim)

