ACL Swarm
=========

ROS implementation of a formation flying pipeline with distributed motion planning, decentralized task assignment, and onboard localization---*all running onboard each vehicle*.

In this pipeline, an operator dispatches a desired formation shape to a team of multirotors. Each multirotor needs only the relative translation to each of its neighbors, a subset of the team defined by the desired formation. These relative translations make up a vehicle's local knowledge and can be measured using methods such as RGB-D sensors or communication.

Upon receiving a desired formation (or to break gridlocks), we would like multirotors to assign themselves to a point in the formation that is closest to their current position. Because each vehicle only has *local* knowledge of the team, we present a decentralized task assignment algorithm wherein each vechicle auctions on its preferred formation points using only its understanding of its neighbors positions. Using consensus, global convergence is achieved with limited communication.

Using these techniques with an off-the-shelf VIO/SLAM system, large aerial formations can be achieved without external infrastructure or global knowledge.

## Video

<p align="center">
    <a href="https://www.youtube.com/watch?v=il0UJCyiAzY"><img src=".github/aclswarm_thumb.png" alt="ACL swarm video" /></a>
</p>

## Paper
If this pipeline is useful, please cite:

P. Lusk, X. Cai, S. Wadhwania, A. Paris, K. Fathian, J. How, "[A Distributed Pipeline for Scalable, Deconflicted Formation Flying](https://arxiv.org/abs/2003.01851)", arXiv:2003.01851.

## Getting Started

The `aclswarm` pipeline leverages ROS and the MIT ACL [custom autopilot stack](https://gitlab.com/mit-acl/fsw/snap-stack). This stack targets the [Qualcomm Snapdragon Flight](https://developer.qualcomm.com/hardware/qualcomm-flight-pro) and supports software-in-the-loop (SIL) simulation.

This software has been tested with ROS Indigo, Kinetic and Melodic. We assume your preferred flavors of Ubuntu/ROS have already been [installed](http://wiki.ros.org/ROS/Installation).

### Simulation

To get started, create a new workspace and use [wstool](http://wiki.ros.org/wstool) to setup the necessary dependencies.

```bash
$ mkdir -p aclswarm_ws/src && cd aclswarm_ws/src
$ catkin init && catkin config --extend /opt/ros/melodic # or whatever
$ git clone https://github.com/mit-acl/aclswarm
$ wstool init
$ wstool merge ./aclswarm/.simulation.rosinstall.https
$ wstool up -j8
```

Then, build and source your workspace as usual

```bash
$ catkin build
$ source ../devel/setup.bash
```

To run a simulation (referred to as a *trial*) use the following incantation. There must **not** be a `roscore` running before you run this step.

```bash
$ rosrun aclswarm_sim trials.sh -f swarm6_3d -i
```

This will spin up a simulation and open an `rviz` window (see **Visualization** section below) and an `rqt_gui` window. Wait for the following message in the terminal (depending on your machine you may need to wait a few seconds after this message for good measure):

```bash
Simulation initialized. You may now press 'START'.
Once rqt_gui (7260) is quit, simulation will clean up.
```

Follow the instructions in the **Operation** section below. Note that exiting the `rqt_gui` window will quit the entire simulation.

### Hardware

### Operation

To run an experiment, follow this sequence:

1. Arm each of the vehicles. *In simulation the vehicles are armed by default.*
2. Press **START** in the `rqt_gui` window and the vehicles will take off to their `takeoff_alt`.
3. Each subsequent click of the **START** button causes the swarm to cycle through the formations defined in the formation group (e.g., `swarm6_3d`).
4. Press **END** to gracefully land the vehicles in their current x-y position.
5. Press **E-STOP** to emergency stop, which immediately kills the motors.

### Visualization

We use rviz to visualize the formation. In the image below, the elements represented are as follows

  - **Quadrotor meshes**: the position of each vehicle in the (unknown) global frame
  - **Black spheres**: the desired formation aligned to the swarm via centralized Arun's method. These are **only for debugging** and this data is not used in the pipeline. That is why the UAVs often do not end up at these points; instead, they serve to give a rough indication of what the swarm is doing.
  - **Blue lines**: Desired command calculated by the distributed motion planning on-board each UAV.
  - **Red lines**: Output of `safety` node --- takes the desired command (blue lines) and implements collision avoidance if needed.

<p align="center">
  <img src=".github/aclswarm_sim.png" alt="ACL swarm rviz visualization" />
</p>
