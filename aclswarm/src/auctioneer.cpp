/**
 * @file auctioneer.cpp
 * @brief Distributed task assignment for aclswarm via CBAA
 * @author Parker Lusk <parkerclusk@gmail.com>
 * @date 26 Oct 2019
 */

#include "aclswarm/auctioneer.h"

namespace acl {
namespace aclswarm {

Auctioneer::Auctioneer(vehidx_t vehid, uint8_t n)
: n_(n), vehid_(vehid), auctionid_(-1), bid_(new Bid)
{
  reset();
}

// ----------------------------------------------------------------------------

void Auctioneer::setNewAssignmentHandler(
                                std::function<void(const AssignmentPerm&)> f)
{
  fn_assignment_ = f;
}

// ----------------------------------------------------------------------------

void Auctioneer::setSendBidHandler(std::function<void(uint32_t, uint32_t,
                                          const Auctioneer::BidConstPtr&)> f)
{
  fn_sendbid_ = f;
}

// ----------------------------------------------------------------------------

void Auctioneer::setFormation(const PtsMat& p, const AdjMat& adjmat)
{
  assert(n_ == p.rows());
  assert(n_ == adjmat.rows());

  p_ = p;
  adjmat_ = adjmat;

  constexpr uint32_t diameter = 2; // hardcoded for now...
  cbaa_max_iter_ = n_ * diameter;

  // reset internal state
  reset();

  // the next auction will be the first to use this newly specified formation
  formation_just_received_ = true;

  // reset the assignment to identity
  P_.setIdentity(n_);
  Pt_.setIdentity(n_);
}

// ----------------------------------------------------------------------------

void Auctioneer::start(const PtsMat& q)
{
  std::lock_guard<std::mutex> lock(auction_mtx_);

  // reset internal state
  reset();

  // Account for any START bids we received before we officially started
  bids_curr_ = bids_zero_;
  bids_zero_.clear();

  // store the current state of nbrs to be used throughout the auction
  q_ = q;

  //
  // Alignment: use my local info to fit the formpts to me and my nbrs
  //

  // align current swarm positions to desired formation (using me & nbrs only)
  paligned_ = alignFormation(q_, adjmat_, p_);

  //
  // Assignment (kick off with an initial bid)
  //

  // Using only knowledgde of my current state and what I think the aligned
  // formation is, make an initial bid for the formation point I am closest to.
  selectTaskAssignment();

  // allow processing of received bids from my neighbors
  auction_is_open_ = true;
  auctionid_++;

  std::cout << std::endl;
  std::cout << "********* Starting auction " << auctionid_ << " *********";
  std::cout << std::endl << std::endl;

  // send my START bid to my neighbors
  notifySendBid();
}

// ----------------------------------------------------------------------------

void Auctioneer::enqueueBid(vehidx_t vehid, uint32_t auctionid, uint32_t iter,
                            const Bid& bid)
{
  std::lock_guard<std::mutex> lock(queue_mtx_);
  rxbids_.emplace(vehid, auctionid, iter, bid);

  std::cout << "A" << auctionid_ << "B" << biditer_ << ": Enqueued ";
  std::cout << "a" << auctionid  << "b" <<    iter  << " from " << static_cast<int>(vehid);
  std::cout << std::endl;
}

// ----------------------------------------------------------------------------

void Auctioneer::tick()
{
  BidPkt bidpkt;

  // if (auction_is_open_) std::cout << "Missing (tick): " << reportMissing() << std::endl;

  {
    std::lock_guard<std::mutex> lock(queue_mtx_);

    // nothing to do anything if there are no bids to process
    if (rxbids_.size() == 0) return;

    // get the oldest bid in the queue (copy)
    bidpkt = rxbids_.front();

    // remove the bid we are about to process
    rxbids_.pop();
  }

  processBid(bidpkt);
}

// ----------------------------------------------------------------------------

std::string Auctioneer::reportMissing()
{
  // work in "formation space" since we are using the adjmat to check nbhrs
  const vehidx_t i = P_.indices()(vehid_);

  std::string missing = "(A" + std::to_string(auctionid_) + "B" + std::to_string(biditer_) + ") ";

  for (size_t j=0; j<n_; ++j) {
    if (adjmat_(i, j)) {
      // map back to "vehicle space" since that's how our bids are keyed
      vehidx_t nbr = Pt_.indices()(j);

      // CBAA iteration is not complete if I am missing any of my nbrs' bids
      if (bids_curr_.find(nbr) == bids_curr_.end()) {
        missing += std::to_string(nbr) + " ";
      }
    }
  }

  return missing;
}

// ----------------------------------------------------------------------------
// Private Methods
// ----------------------------------------------------------------------------

void Auctioneer::notifySendBid()
{
  // let the caller know
  fn_sendbid_(auctionid_, biditer_, bid_);
}

// ----------------------------------------------------------------------------

void Auctioneer::notifyNewAssignment()
{
  // let the caller know
  fn_assignment_(P_);
}

// ----------------------------------------------------------------------------

void Auctioneer::processBid(const BidPkt& bidpkt)
{
  std::lock_guard<std::mutex> lock(auction_mtx_);

  // unpack bid packet
  const auto& vehid = std::get<0>(bidpkt);
  const auto& auctionid = std::get<1>(bidpkt);
  const auto& iter = std::get<2>(bidpkt);
  const auto& bid = std::get<3>(bidpkt);

  std::cout << "A" << auctionid_ << "B" << biditer_ << ": Processing ";
  std::cout << "a" << auctionid  << "b" <<    iter  << " from " << static_cast<int>(vehid);
  std::cout << " [missing " << reportMissing() << "]";
  std::cout << std::endl;

  // always save the START bid in a special bucket in case we haven't started
  // yet. That way we don't blow it away when we start and do a reset.
  if (iter == 0) bids_zero_.insert({vehid, bid});

  // if the auction is not yet opened, there is nothing to do (we may not even
  // have an adjmat yet!). If we receive any START bids from our neighbors,
  // we will assume that they are from an auction that is about to open---we
  // are just slower to start than the others. These START bids (in bids_zero_)
  // will not be lost.
  // Further, we should not see any bids from iter>0 since our nbrs would need
  // our START bid in order to advance to the next bid iteration.
  if (!auction_is_open_) return;

  // put incoming bids into the right bucket. Because CBAA needs all nbrs to
  // respond before it can proceed, we should never see a bid from an iteration
  // more than one ahead of us. If we do, it would be a START (zero) bid.
  if (iter == biditer_) bids_curr_.insert({vehid, bid});
  else if (iter == biditer_+1) bids_next_.insert({vehid, bid});
  else std::cout << "!! Threw away A" << auctionid << "B" << iter << " from " << static_cast<int>(vehid) << std::endl;

  // once my neighbors' bids are in, tally them up and decide who the winner is
  if (bidIterComplete()) {

    //
    // Update CBAA
    //

    // update my local understanding of who deserves which task based on the
    // highest bidder of each task, within my neighborhood.
    bool was_outbid = updateTaskAssignment();

    // If I was outbid, I will need to select a new task.
    if (was_outbid) selectTaskAssignment();

    // start the next iteration
    ++biditer_;

    //
    // House cleaning
    //

    // account for any bids we received but weren't ready for
    bids_curr_ = bids_next_;
    bids_next_.clear();

    // clear after iter zero is complete so we are ready for next START bids
    if (biditer_ == 1) bids_zero_.clear();

    //
    // Determine convergence or continue bidding
    //

    if (hasReachedConsensus()) {
      // Extract the best assignment from my local understanding,
      // which has reached consensus since the auction is complete.

      // note: we are making implicit type casts here
      std::vector<vehidx_t> tmp(bid_->who.begin(), bid_->who.end());

      // n.b., 'who' maps task --> vehid, which is P^T
      const auto newPt = AssignmentPerm(Eigen::Map<AssignmentVec>(tmp.data(), tmp.size()));
      const auto newP = newPt.transpose();

      // log the assignment for debugging
      logAssignment(q_, adjmat_, p_, paligned_, P_, newP);

      // determine if this assignment is better than the previous one
      if (shouldUseAssignment(newP)) {

        // set the assignment
        P_ = newP;
        Pt_ = newPt;

        // let the caller know a new assignment is ready
        notifyNewAssignment();
      }

      // get ready for next auction, makes auctioneer idle
      reset();
    } else {
      // send latest bid to my neighbors
      notifySendBid();
    }
  }
}

// ----------------------------------------------------------------------------

bool Auctioneer::shouldUseAssignment(const AssignmentPerm& newP) /*const*/
{
  // make sure the assignment is a one-to-one correspondence
  // TODO: this check could be removed once auction IDs are checked
  if ((newP.toDenseMatrix().rowwise().sum().array() != 1).any() ||
        (newP.toDenseMatrix().colwise().sum().array() != 1).any()) {
    std::cout << std::endl << "\033[95;1m!!!! Invalid Assignment !!!!\033[0m";
    std::cout << std::endl << std::endl;
    stopTimer_ = true;
    return false;
  }

  if (formation_just_received_) {
    formation_just_received_ = false;
    return true;
  }

  // don't bother if the assignment is the same
  if (P_.indices().isApprox(newP.indices())) return false;

  return true;
}

// ----------------------------------------------------------------------------

PtsMat Auctioneer::alignFormation(const PtsMat& q,
                                  const AdjMat& adjmat, const PtsMat& p) const
{
  // Find (R,t) that minimizes ||q - (Rp + t)||^2

  //
  // Select Local Information (i.e., use only nbrs for alignment)
  //

  // work in "formation space" since we are using the adjmat to check nbhrs
  const vehidx_t i = P_.indices()(vehid_);

  // keep track this vehicle's neighbors ("formation space")---include myself
  std::vector<vehidx_t> nbrpts;
  for (size_t j=0; j<n_; ++j) if (adjmat(i, j) || i==j) nbrpts.push_back(j);

  // extract local nbrhd information for this vehicle to use in alignment
  PtsMat pnbrs = PtsMat::Zero(nbrpts.size(), 3);
  PtsMat qnbrs = PtsMat::Zero(nbrpts.size(), 3);
  for (size_t k=0; k<nbrpts.size(); ++k) {
    // correspondence btwn nbr formpts and nbr vehicles
    pnbrs.row(k) = p.row(nbrpts[k]);
    qnbrs.row(k) = q.row(Pt_.indices()(nbrpts[k]));
  }

  //
  // Determine if the formation is a colinear, flat, or 3d
  //

  // be very stringent on what we consider a nonzero singular value (for noise)
  constexpr double RANK_TH = 0.05;
  Eigen::JacobiSVD<PtsMat> svdQ(qnbrs.rowwise() - qnbrs.colwise().mean());
  Eigen::JacobiSVD<PtsMat> svdP(pnbrs.rowwise() - pnbrs.colwise().mean());
  svdQ.setThreshold(RANK_TH);
  svdP.setThreshold(RANK_TH);

  // Only perform 3D Umeyama if there is structure in both swarm and formation
  int d = (svdQ.rank() == 3 && svdP.rank() == 3) ? 3 : 2;

  //
  // Point cloud alignment (maps p onto q)
  //

  // We need our point clouds to be stored in 3xN matrices
  const Eigen::Matrix<double, 3, Eigen::Dynamic> pp = pnbrs.transpose();
  const Eigen::Matrix<double, 3, Eigen::Dynamic> qq = qnbrs.transpose();

  const auto T = Eigen::umeyama(pp.topRows(d), qq.topRows(d), false);

  // for the extracted transformation that maps p onto q
  Eigen::Matrix3d R = Eigen::Matrix3d::Identity();
  Eigen::Vector3d t = Eigen::Vector3d::Zero();

  // Make sure to embed back in 3D if necessary
  if (d == 2) {
    R.block<2,2>(0,0) = T.block<2,2>(0,0);
    t.head(2) = T.block<2,1>(0,2);
  } else {
    R.block<3,3>(0,0) = T.block<3,3>(0,0);
    t = T.block<3,1>(0,3);
  }

  // make sure to send back as an Nx3 PtsMat
  PtsMat aligned = ((R * p.transpose()).colwise() + t).transpose();
  return aligned;
}

// ----------------------------------------------------------------------------

bool Auctioneer::bidIterComplete() const
{
  // work in "formation space" since we are using the adjmat to check nbhrs
  const vehidx_t i = P_.indices()(vehid_);

  for (size_t j=0; j<n_; ++j) {
    if (adjmat_(i, j)) {
      // map back to "vehicle space" since that's how our bids are keyed
      vehidx_t nbr = Pt_.indices()(j);

      // CBAA iteration is not complete if I am missing any of my nbrs' bids
      if (bids_curr_.find(nbr) == bids_curr_.end()) {
        return false;
      }
    }
  }

  return true;
}

// ----------------------------------------------------------------------------

bool Auctioneer::hasReachedConsensus() const
{
  return biditer_ >= cbaa_max_iter_;
}

// ----------------------------------------------------------------------------

void Auctioneer::reset()
{
  // the auctioneer is not ready to receive bids
  auction_is_open_ = false;

  // Ready to receive/send any START bids
  biditer_ = 0;

  // initialize my bid
  bid_->price.clear();
  std::fill_n(std::back_inserter(bid_->price), n_, 0.0);
  bid_->who.clear();
  std::fill_n(std::back_inserter(bid_->who), n_, -1);

  // initialize the price tables that will hold current and next iter bids
  bids_curr_.clear();
  bids_next_.clear();
}

// ----------------------------------------------------------------------------

bool Auctioneer::updateTaskAssignment()
{
  // Given all of the bids from my neighbors (and me), which agent is most
  // deserving of each of the formation points (tasks)? In other words,
  // At this point in the current auction, who currently has the highest bids?
  // n.b., a nbhr might have *info* about who has the highest bid for a given
  // formpt, but it may not be that nbhr---it's just in their local info

  bool was_outbid = false;

  // add myself so that my local information is considered
  bids_curr_.insert({vehid_, *bid_});

  // loop through each task and decide on the winner
  for (size_t j=0; j<n_; ++j) {

    //
    // Which of my nbhrs (or me) bid the most for task / formpt j?
    //

    // arbitrarily assume that the first nhbr in the list has the highest bid
    auto maxit = bids_curr_.cbegin();

    // but then loop through each nbhr (and me) and decide who bid the most
    for (auto it = bids_curr_.cbegin(); it!=bids_curr_.cend(); it++) {
      if (it->second.price[j] > maxit->second.price[j]) maxit = it;
    }

    //
    // Update my local understanding of who has bid the most for each task
    //

    // check if I was outbid by someone else
    if (bid_->who[j] == vehid_ && maxit->second.who[j] != vehid_) was_outbid = true;

    // who should be assigned task j?
    bid_->who[j] = maxit->second.who[j];

    // how much is this winning agent willing to bid on this task?
    bid_->price[j] = maxit->second.price[j];
  }

  // did someone outbid me for my desired formation point / task?
  return was_outbid;
}

// ----------------------------------------------------------------------------

void Auctioneer::selectTaskAssignment()
{
  // Determine the highest price this agent is willing to pay to be assigned
  // a specific task / formpt.
  float max = 0;
  size_t task = 0;
  bool was_assigned = false;
  for (size_t j=0; j<n_; ++j) {
    // n.b., within the same auction, this list of prices will be the same
    const float price = getPrice(q_.row(vehid_), paligned_.row(j));

    // In addition to finding the task that I am most interested in,
    // only bid on a task if I think I will win (highest bidder of my nbhrs)
    if (price > max && price > bid_->price[j]) {
      max = price;
      task = j;
      was_assigned = true;
    }
  }

  // update my local information to reflect my bid
  if (was_assigned) { // TODO: will this always be true?
    bid_->price[task] = max;
    bid_->who[task] = vehid_;
  }
}

// ----------------------------------------------------------------------------

float Auctioneer::getPrice(const Eigen::Vector3d& p1, const Eigen::Vector3d& p2)
{
  return 1.0 / ((p1 - p2).norm() + 1e-8);
}

// ----------------------------------------------------------------------------

void Auctioneer::logAssignment(const PtsMat& q, const AdjMat& adjmat,
                        const PtsMat& p, const PtsMat& aligned,
                        const AssignmentPerm& lastP, const AssignmentPerm& P)
{
  // open a binary file
  std::ofstream bin("alignment_" + std::to_string(vehid_) + ".bin",
                    std::ios::binary);

  bin.write(reinterpret_cast<const char *>(&n_), sizeof n_);
  bin.write(reinterpret_cast<const char *>(q.data()), sizeof(q.data()[0])*q.size());
  bin.write(reinterpret_cast<const char *>(adjmat.data()), sizeof(adjmat.data()[0])*adjmat.size());
  bin.write(reinterpret_cast<const char *>(lastP.indices().data()), sizeof(lastP.indices().data()[0])*lastP.indices().size());
  bin.write(reinterpret_cast<const char *>(p.data()), sizeof(p.data()[0])*p.size());
  bin.write(reinterpret_cast<const char *>(aligned.data()), sizeof(aligned.data()[0])*aligned.size());
  bin.write(reinterpret_cast<const char *>(P.indices().data()), sizeof(P.indices().data()[0])*P.indices().size());
  bin.close();
}

} // ns aclswarm
} // ns acl
