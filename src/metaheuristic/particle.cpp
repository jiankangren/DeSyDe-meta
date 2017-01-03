#include "particle.hpp"
Particle::Particle(shared_ptr<Mapping> _mapping, shared_ptr<Applications> _application, 
                    int _objective, float _w_s, float _w_lb, float _w_gb):
                    mapping(_mapping),
                    applications(_application),
                    no_entities(mapping->getNumberOfApps()),
                    no_actors(applications->n_SDFActors()),
                    no_channels(applications->n_SDFchannels()),
                    no_processors(mapping->getPlatform()->nodes()),
                    no_tdma_slots(mapping->getPlatform()->tdmaSlots()),
                    objective(_objective),
                    current_position(),
                    best_local_position(),
                    best_global_position(),
                    speed(no_actors, no_channels, no_processors),
                    w_s(_w_s),
                    w_lb(_w_lb),
                    w_gb(_w_gb),
                    no_invalid_moves(0)                    
{   
    init_random();    
}
void Particle::build_schedules(Position& p)
{
    p.proc_sched.clear();
    p.send_sched.clear();
    p.rec_sched.clear();
    for(size_t i=0;i<no_processors;i++)
    {
        p.proc_sched.push_back(Schedule(p.get_actors_by_proc(i), i+no_actors));
        
        p.send_sched.push_back(Schedule(get_channel_by_src(p, i), i+no_channels));
        
        p.rec_sched.push_back(Schedule(get_channel_by_dst(p, i), i+no_channels));
    }
}
void Particle::repair(Position &p)
{
    p.proc_mappings = bring_v_to_bound(p.proc_mappings, 0, no_processors-1);
     for(size_t i=0;i<no_processors;i++)
    {
       int no_proc_modes = mapping->getPlatform()->getModes(i);
       p.proc_modes[i] = bring_to_bound(p.proc_modes[i], 0, no_proc_modes-1);
    }
    repair_tdma(p);    
    repair_sched(p);
    repair_send_sched(p);
    repair_rec_sched(p);
}
void Particle::repair_sched(Position& p)
{
    for(size_t proc=0;proc<p.proc_sched.size();proc++)
    {
        p.proc_sched[proc].set_rank( bring_v_to_bound(p.proc_sched[proc].get_rank(), 0, p.proc_sched[proc].get_rank().size()-1) );        
        for(size_t i=0;i<p.proc_sched[proc].get_elements().size();i++)
        {
            int a = p.proc_sched[proc].get_elements()[i];
            for(size_t j=i;j<p.proc_sched[proc].get_elements().size();j++)
            {                
                int b = p.proc_sched[proc].get_elements()[j];
                int rank_a = p.proc_sched[proc].get_rank_by_id(i);
                int rank_b = p.proc_sched[proc].get_rank_by_id(j);
                /**
                 * if a and b are not dummy and the dependency is violated
                 */ 
                if((a < (int) no_actors && b < (int) no_actors) && rank_a > rank_b && applications->dependsOn(a, b))
                {
                    p.proc_sched[proc].switch_ranks(i, j);
                    LOG_DEBUG("switching " + tools::toString(a) +
                              " and " + tools::toString(b));
                }
            }
        }
    }
    
}
void Particle::repair_send_sched(Position& p)
{
    for(size_t proc=0;proc<p.send_sched.size();proc++)
    {
        p.send_sched[proc].set_rank( bring_v_to_bound(p.send_sched[proc].get_rank(), 0, p.send_sched[proc].get_rank().size()-1) );
        for(size_t i=0;i<p.send_sched[proc].get_elements().size();i++)
        {
            int a = p.send_sched[proc].get_elements()[i];
            for(size_t j=0;j<p.send_sched[proc].get_elements().size();j++)
            {                
                int b = p.send_sched[proc].get_elements()[j];
                if(a < (int) no_channels && b < (int) no_channels)
                {
                    int dst_b = applications->getChannel(b)->destination;
                    int dst_a = applications->getChannel(a)->destination;
                    int src_b = applications->getChannel(b)->source;
                    int src_a = applications->getChannel(a)->source;
                    int rank_a = p.send_sched[proc].get_rank_by_id(i);
                    int rank_b = p.send_sched[proc].get_rank_by_id(j);
                    int rank_src_a = p.proc_sched[p.proc_mappings[src_a]].get_rank_by_element(src_a);
                    int rank_src_b = p.proc_sched[p.proc_mappings[src_b]].get_rank_by_element(src_b);
                    /**
                     * if a and b are sending to the same destination
                     * and the dependency is violated
                     * OR 
                     * if their source is on the same proc and 
                     * rank_src_a < rank_src_b and rank_a > rank_b
                     */ 
                    if(
                      (dst_a == dst_b
                        && rank_a < rank_b && applications->dependsOn(dst_a, dst_b))
                       ||
                       (rank_src_a < rank_src_b && rank_a > rank_b) 
                      )
                    {
                        p.send_sched[proc].switch_ranks(i, j);
                        LOG_DEBUG("switching send_next " + tools::toString(a) +
                                  " and " + tools::toString(b));
                    }
                }
            }
        }
    }
    
}
void Particle::repair_rec_sched(Position& p)
{
    /**
     * if the source of a and b are on the same proc
     * and rank_src_a < rank_src_b and rank_a > rank_b then switch
     */ 
    for(size_t proc=0;proc<p.rec_sched.size();proc++)
    {
        p.rec_sched[proc].set_rank( bring_v_to_bound(p.rec_sched[proc].get_rank(), 0, p.rec_sched[proc].get_rank().size()-1) );
        for(size_t i=0;i<p.rec_sched[proc].get_elements().size();i++)
        {
            int a = p.rec_sched[proc].get_elements()[i];
            for(size_t j=0;j<p.rec_sched[proc].get_elements().size();j++)
            {                
                int b = p.rec_sched[proc].get_elements()[j];
                if(a < (int) no_channels && b < (int) no_channels)
                {
                    int src_b = applications->getChannel(b)->source;
                    int src_a = applications->getChannel(a)->source;
                    if(p.proc_mappings[src_a] == p.proc_mappings[src_b])
                    {
                        int rank_src_a = p.send_sched[p.proc_mappings[src_a]].get_rank_by_element(a);
                        int rank_src_b = p.send_sched[p.proc_mappings[src_b]].get_rank_by_element(b);       
                        int rank_a = p.rec_sched[proc].get_rank_by_id(i);
                        int rank_b = p.rec_sched[proc].get_rank_by_id(j);            
                    
                        if(rank_src_a < rank_src_b && rank_a > rank_b)
                        {
                            p.rec_sched[proc].switch_ranks(i, j);
                            LOG_DEBUG("switching rec_next " + tools::toString(a) +
                                      " and " + tools::toString(b));
                        }
                    }
                }
            }
        }
    }
    
}
vector<int> Position::get_actors_by_proc(int proc_id) const
{
    vector<int> actors;
    for(size_t i=0;i<proc_mappings.size();i++)
    {
        if(proc_mappings[i] == proc_id)
            actors.push_back(i);
    }
    return actors;
}
vector<int> Particle::get_channel_by_src(Position& p, int src_proc_id) const
{
    vector<int> channels;
    for(size_t i=0;i<no_channels;i++)
    {
        if(p.proc_mappings[applications->getChannel(i)->source] == src_proc_id)
            channels.push_back(i);
    }
    return channels;
}
vector<int> Particle::get_channel_by_dst(Position& p, int dst_proc_id) const
{
    vector<int> channels;
    for(size_t i=0;i<no_channels;i++)
    {
        if(p.proc_mappings[applications->getChannel(i)->destination] == dst_proc_id)
            channels.push_back(i);
    }
    return channels;
}
void Particle::init_random()
{
    /** clear. */
    current_position.proc_mappings.clear();
    current_position.proc_modes.clear();
    current_position.tdmaAlloc.clear();
    current_position.fitness.clear();
    current_position.proc_sched.clear();
    current_position.send_sched.clear();
    current_position.rec_sched.clear();
    no_invalid_moves = 0;
    
    random_device rnd_device;
    mt19937 mersenne_engine(rnd_device());
    uniform_int_distribution<int> dist_proc(0, no_processors-1);
    uniform_int_distribution<int> dist_actor(0, no_actors-1);
    uniform_int_distribution<int> dist_channel(0, no_channels-1);
    uniform_int_distribution<int> dist_tdma(0, no_tdma_slots);

    /// the engine has to be reset after crreating the distribution
    auto gen_proc = std::bind(dist_proc, mersenne_engine);mersenne_engine();
    auto gen_tdma = std::bind(dist_proc, mersenne_engine);mersenne_engine();
    
    current_position.proc_mappings.resize(no_actors, 0);
    current_position.proc_modes.resize(no_processors, 0);
    current_position.tdmaAlloc.resize(no_processors, 0);
    
    generate(begin(current_position.proc_mappings), end(current_position.proc_mappings), gen_proc); 
    generate(begin(current_position.tdmaAlloc), end(current_position.tdmaAlloc), gen_tdma);
    
    ///Random proc_modes based on the number of modes for each processors
    for(size_t i=0;i<no_processors;i++)
    {
       int no_proc_modes = mapping->getPlatform()->getModes(i);
       std::uniform_int_distribution<int> uni_dist(0,no_proc_modes-1);
       current_position.proc_modes[i] = uni_dist(mersenne_engine);       
    }
    
    
    build_schedules(current_position);
    current_position.fitness.resize(no_entities + 2,0);///energy + memory violations + throughputs    
    repair(current_position);         
}
void Particle::repair_tdma(Position& p)
{
    vector<int> no_inout_channels(no_processors, 0);
    ///Random # tdma slots based on src and dst of channels
    for(size_t i=0;i<no_channels;i++)
    {
        int src_i = applications->getChannel(i)->source;
        int dest_i = applications->getChannel(i)->destination;
        int proc_src_i = p.proc_mappings[src_i];
        int proc_dest_i = p.proc_mappings[dest_i];    
        if(proc_src_i != proc_dest_i)
        {
            no_inout_channels[proc_src_i]++;
            no_inout_channels[proc_dest_i]++;
        }      
    }
    for(size_t i=0;i<no_inout_channels.size();i++)
    {
        if(no_inout_channels[i] == 0)
            p.tdmaAlloc[i] = 0;
        else
        {
            if(p.tdmaAlloc[i] == 0)
            {
                p.tdmaAlloc[i]++;
            }
        }    
    }
    /**
     * If the total number of allocated slots is more than the number of 
     * available slots, then take the difference away from some processors
     */     
    int sum_of_elems = std::accumulate(p.tdmaAlloc.begin(), p.tdmaAlloc.end(), 0);
    int diff = sum_of_elems - no_tdma_slots;
    while(diff > 0)
    {
        for(size_t i=0;i<p.tdmaAlloc.size();i++)
        {
            if(p.tdmaAlloc[i] > 1)
            {
                p.tdmaAlloc[i]--;
                diff--;
                if(diff <= 0)
                    break;
            }
        }
    }
}
vector<int> Particle::get_next(vector<Schedule> sched_set, int no_elements)
{
    vector<int> next(no_elements+no_processors, 0);
    vector<int> low_ranks;
    for (auto s : sched_set)
    {
        for(auto e: s.get_elements())
        {
            next[e] = s.get_next(e);
        }
        low_ranks.push_back(s.get_element_by_rank(0));                    
    }
    ///Last dummy node should point to the highest rank of the first proc
    next[no_elements+no_processors-1] = low_ranks[0];
    for(size_t i=0;i<low_ranks.size()-1;i++)
    {
        next[no_elements+i] = low_ranks[i+1];
    }
    return next;
}
void Particle::calc_fitness()
{   
    try{ 
        Design design(mapping, applications, current_position.proc_mappings, 
                      current_position.proc_modes, get_next(current_position.proc_sched, no_actors),
                      get_next(current_position.send_sched, no_channels),
                      get_next(current_position.rec_sched, no_channels), 
                      current_position.tdmaAlloc);
        
        vector<int> prs = design.get_periods();
      

        current_position.fitness.clear();
        current_position.fitness.resize(no_entities + 2,0);
        int eng = design.get_energy();
        for(size_t i=0;i< prs.size();i++)
            current_position.fitness[i] = prs[i];
            
        current_position.fitness[current_position.fitness.size()-2] = eng;
        int no_mem_violations = 0;
        for(auto m : design.get_slack_memory())
            if(m < 0)
                no_mem_violations++;
                    
        current_position.fitness[current_position.fitness.size()-1] = no_mem_violations;        
    }
    catch(std::exception const& e)
    {
        cout << *this << endl;
        THROW_EXCEPTION(RuntimeException, e.what() );
    }
    if(dominate(current_position.fitness))
    {   
        best_local_position = current_position;
    }
    if(current_position.fitness[0] < 0)
        {
            no_invalid_moves++;
            //cout << "no_invalid_moves:" << no_invalid_moves << endl;
        }
    else
        no_invalid_moves = 0;        
    
}
vector<int> Particle::get_fitness()
{
    return current_position.fitness;
}
void Particle::set_best_global(Position p)
{
    best_global_position = p;
}
Position Particle::get_current_position()
{
    return current_position;
}
Position Particle::get_best_local_position()
{
    return best_local_position;
}
Speed Particle::get_speed()
{
    return speed;
}
void Particle::update_position()
{   
    
    /**
     * In case the number of invalid moves is more than the threshold,
     * then reinitialize the particle.
     */
    if(no_invalid_moves > thr_invalid_mov)  
        init_random();
        
    update_speed();
    /*
    if(!best_global_position.empty() && !best_local_position.empty())
        add_positions(current_position, best_local_position, best_global_position, .3, .3);
    else
    {    
        if(!best_global_position.empty())
            add_positions(current_position, best_global_position, .5);
        
        if(!best_local_position.empty())
            add_positions(current_position, best_local_position, .5);
    }*/
    move();
    repair(current_position);    
}
float Particle::random_weight()
{
    random_device rnd_device;
    uniform_int_distribution<int> dist(0, 100);
    mt19937 mersenne_engine(rnd_device());  
    auto gen = std::bind(dist, mersenne_engine);
    float w = ((float)gen())/100;
    return w;    
}
void Particle::update_speed()
{
    if(best_global_position.empty())
        THROW_EXCEPTION(RuntimeException, "update_speed: best_global is empty" );
    if(best_local_position.empty())
        THROW_EXCEPTION(RuntimeException, "update_speed: best_local is empty" );
    float y1 = random_weight();    
    float y2 = random_weight();
    
    for(size_t i=0;i<speed.proc_mappings.size();i++)
    {
        speed.proc_mappings[i] = w_s * speed.proc_mappings[i] +
                                 y1 * w_lb * (best_local_position.proc_mappings[i] - current_position.proc_mappings[i]) +
                                 y2 * w_gb * (best_global_position.proc_mappings[i] - current_position.proc_mappings[i]);
    }
    for(size_t i=0;i<speed.proc_modes.size();i++)
    {
        speed.proc_modes[i] = w_s * speed.proc_modes[i] +
                                 y1 * w_lb * (best_local_position.proc_modes[i] - current_position.proc_modes[i]) +
                                 y2 * w_gb * (best_global_position.proc_modes[i] - current_position.proc_modes[i]);
    }
    for(size_t i=0;i<speed.tdmaAlloc.size();i++)
    {
        speed.tdmaAlloc[i] = w_s * speed.tdmaAlloc[i] +
                                 y1 * w_lb * (best_local_position.tdmaAlloc[i] - current_position.tdmaAlloc[i]) +
                                 y2 * w_gb *  (best_global_position.tdmaAlloc[i] - current_position.tdmaAlloc[i]);
    }
    for(size_t i=0;i<speed.proc_sched.size();i++)
    {
        int cu_p = current_position.proc_mappings[i];
        int bl_p = best_local_position.proc_mappings[i];
        int bg_p = best_global_position.proc_mappings[i];
        speed.proc_sched[i] = w_s * speed.proc_sched[i] +
                                 y1 * w_lb * (best_local_position.proc_sched[bl_p].get_rank_by_element(i) - current_position.proc_sched[cu_p].get_rank_by_element(i)) +
                                 y2 * w_gb * (best_global_position.proc_sched[bg_p].get_rank_by_element(i) - current_position.proc_sched[cu_p].get_rank_by_element(i));
    }
    for(size_t i=0;i<speed.send_sched.size();i++)
    {
        int src = applications->getChannel(i)->source;
        int cu_p = current_position.proc_mappings[src];
        int bl_p = best_local_position.proc_mappings[src];
        int bg_p = best_global_position.proc_mappings[src];
        speed.send_sched[i] = w_s * speed.send_sched[i] +
                                 y1 * w_lb * (best_local_position.send_sched[bl_p].get_rank_by_element(i) - current_position.send_sched[cu_p].get_rank_by_element(i)) +
                                 y2 * w_gb * (best_global_position.send_sched[bg_p].get_rank_by_element(i) - current_position.send_sched[cu_p].get_rank_by_element(i));
    }
    for(size_t i=0;i<speed.rec_sched.size();i++)
    {
        int dst = applications->getChannel(i)->destination;
        int cu_p = current_position.proc_mappings[dst];
        int bl_p = best_local_position.proc_mappings[dst];
        int bg_p = best_global_position.proc_mappings[dst];
        speed.rec_sched[i] = w_s * speed.rec_sched[i] +
                                 y1 * w_lb * (best_local_position.rec_sched[bl_p].get_rank_by_element(i) - current_position.rec_sched[cu_p].get_rank_by_element(i)) +
                                 y2 * w_gb * (best_global_position.rec_sched[bg_p].get_rank_by_element(i) - current_position.rec_sched[cu_p].get_rank_by_element(i));
    }
}
void Particle::move() 
{
    for(size_t i=0;i<current_position.proc_mappings.size();i++)
    {
        current_position.proc_mappings[i] = Schedule::random_round((float) current_position.proc_mappings[i] + speed.proc_mappings[i]);                 
    }
    current_position.proc_mappings = bring_v_to_bound(current_position.proc_mappings, 0, no_processors-1);
    for(size_t i=0;i<current_position.proc_modes.size();i++)
    {
        current_position.proc_modes[i] = Schedule::random_round((float) current_position.proc_modes[i] + speed.proc_modes[i]);                 
    }
    for(size_t i=0;i<current_position.tdmaAlloc.size();i++)
    {
        current_position.tdmaAlloc[i] = Schedule::random_round((float) current_position.tdmaAlloc[i] + speed.tdmaAlloc[i]);                 
    }

    build_schedules(current_position);
    /** add proc_sched. */    
    for(size_t proc;proc<current_position.proc_sched.size();proc++)
    {
        ///go through all elemennts in this proc
        for(auto e : current_position.proc_sched[proc].get_elements())
        {
            current_position.proc_sched[proc].set_rank_by_element(e, 
                    Schedule::random_round((float)current_position.proc_sched[proc].get_rank_by_element(e)+
                                            speed.proc_sched[e]) );
        }
    }  
    /** add send_sched. */  
    for(size_t proc;proc<current_position.send_sched.size();proc++)
    {
        ///go through all elemennts in this proc
        for(auto e : current_position.send_sched[proc].get_elements())
        {
            current_position.send_sched[proc].set_rank_by_element(e, 
                    Schedule::random_round((float)current_position.send_sched[proc].get_rank_by_element(e)+
                                            speed.send_sched[e]) );
        }
    } 
    /** add rec_sched. */        
    for(size_t proc;proc<current_position.rec_sched.size();proc++)
    {
        ///go through all elemennts in this proc
        for(auto e : current_position.rec_sched[proc].get_elements())
        {
            current_position.rec_sched[proc].set_rank_by_element(e, 
                    Schedule::random_round((float)current_position.rec_sched[proc].get_rank_by_element(e)+
                                            speed.rec_sched[e]) );
        }
    }              
    
    
}
void Particle::add_positions(Position& p1, const Position& p2, float w) 
{
    if(p1.proc_mappings.size() != p2.proc_mappings.size())
        THROW_EXCEPTION(RuntimeException, "+ operator: proc_mappings have different sizes" );
    if(p1.proc_modes.size() != p2.proc_modes.size())
        THROW_EXCEPTION(RuntimeException, "+ operator: proc_modes have different sizes" );
    if(p1.tdmaAlloc.size() != p2.tdmaAlloc.size())
        THROW_EXCEPTION(RuntimeException, "+ operator: tdmaAlloc have different sizes" );
    
    Position new_p;
    try
    {
        for(size_t i=0;i<p1.proc_mappings.size();i++)
        {
            new_p.proc_mappings.push_back(Position::weighted_sum(p1.proc_mappings[i],
                                            p2.proc_mappings[i], w));            
        }
        for(size_t i=0;i<p1.proc_modes.size();i++)
        {
            new_p.proc_modes.push_back(Position::weighted_sum(p1.proc_modes[i],
                                            p2.proc_modes[i], w)); 
        }
        for(size_t i=0;i<p1.tdmaAlloc.size();i++)
        {
            new_p.tdmaAlloc.push_back(Position::weighted_sum(p1.tdmaAlloc[i],
                                            p2.tdmaAlloc[i], w)); 
        }

        build_schedules(new_p);
        /** add proc_sched. */    
        for(size_t proc;proc<p1.proc_sched.size();proc++)
        {
            ///go through all elemennts in this proc
            for(auto e : p1.proc_sched[proc].get_elements())
            {
                new_p.proc_sched[new_p.proc_mappings[e]].set_rank_by_element(e, 
                        Position::weighted_sum(p1.proc_sched[proc].get_rank_by_element(e),
                                     p2.proc_sched[p2.proc_mappings[e]].get_rank_by_element(e), w) );
            }
        }  
        /** add send_sched. */  
        /** add rec_sched. */        
        p1 = new_p;                
    }
    catch(std::exception const& e)
    {
        cout << new_p << endl;
        THROW_EXCEPTION(RuntimeException, e.what() );
    }  
    
}
void Particle::add_positions(Position& p1, const Position& p2, Position& p3, float w1, float w2) 
{
    if(p1.proc_mappings.size() != p2.proc_mappings.size() || p1.proc_mappings.size() != p3.proc_mappings.size() )
        THROW_EXCEPTION(RuntimeException, "+ operator: proc_mappings have different sizes" );
    if(p1.proc_modes.size() != p2.proc_modes.size() || p1.proc_modes.size() != p3.proc_modes.size())
        THROW_EXCEPTION(RuntimeException, "+ operator: proc_modes have different sizes" );
    if(p1.tdmaAlloc.size() != p2.tdmaAlloc.size() || p1.tdmaAlloc.size() != p3.tdmaAlloc.size() )
        THROW_EXCEPTION(RuntimeException, "+ operator: tdmaAlloc have different sizes" );
    
    Position new_p;
    try
    {
        for(size_t i=0;i<p1.proc_mappings.size();i++)
        {
            new_p.proc_mappings.push_back(Position::weighted_sum(p1.proc_mappings[i],
                                            p2.proc_mappings[i], p3.proc_mappings[i], w1, w2));            
        }
        for(size_t i=0;i<p1.proc_modes.size();i++)
        {
            new_p.proc_modes.push_back(Position::weighted_sum(p1.proc_modes[i],
                                            p2.proc_modes[i], p3.proc_modes[i], w1, w2)); 
        }
        for(size_t i=0;i<p1.tdmaAlloc.size();i++)
        {
            new_p.tdmaAlloc.push_back(Position::weighted_sum(p1.tdmaAlloc[i],
                                            p2.tdmaAlloc[i], p3.tdmaAlloc[i], w1, w2)); 
        }

        build_schedules(new_p);
        /** add proc_sched. */    
        for(size_t proc;proc<p1.proc_sched.size();proc++)
        {
            ///go through all elemennts in this proc
            for(auto e : p1.proc_sched[proc].get_elements())
            {
                new_p.proc_sched[new_p.proc_mappings[e]].set_rank_by_element(e, 
                        Position::weighted_sum(p1.proc_sched[proc].get_rank_by_element(e),
                                     p2.proc_sched[p2.proc_mappings[e]].get_rank_by_element(e),
                                     p3.proc_sched[p3.proc_mappings[e]].get_rank_by_element(e), w1, w2) );
            }
        }        
        p1 = new_p;                
    }
    catch(std::exception const& e)
    {
        cout << new_p << endl;
        THROW_EXCEPTION(RuntimeException, e.what() );
    }
    
    
}
bool Particle::dominate(vector<int> f)
{
    /**
     * if best_local is empty
     * Or
     * it is an illegal position, i.e. throughput is negative and the new position is legal
     * then return true 
     */ 
    if(best_local_position.fitness.empty() || (best_local_position.fitness[objective] < 0 && f[objective] > 0))
        return true;
    if(f[objective] < 0)
        return false;
    if(f[objective] < best_local_position.fitness[objective])
        return true;
    else
        return false;    
                
    /*bool better_pr = true;
    bool better_eng = true;
    bool better_mem = true;
    for(int i=0;i<mapping->getNumberOfApps();i++)
    {
        if(f[i] > best_local_position.fitness[i])
        {
            better_pr = false;
            break;
        }
    }
    //energy
    if(f[f.size()-2] > best_local_position.fitness[best_local_position.fitness.size()-2])
    {
        better_eng = false;
    }
    //memory violations
    if(f[f.size()-1] > best_local_position.fitness[best_local_position.fitness.size()-1])
    {
        better_mem = false;
    }
    if(better_pr && better_eng && better_mem)
        return true;
    else    
        return false;
    */ 
}
vector<int> Particle::bring_v_to_bound(vector<int> v, int l, int u)
{
    vector<int> out;
    for(auto i: v)
        out.push_back(bring_to_bound(i, l, u));
    return out;    
}
int Particle::bring_to_bound(int v, int l, int u)
{
    if(v < l)
        return l;
    if(v > u)
        return u;
    return v;    
}
int Particle::get_objective()
{
    return objective;
}
std::ostream& operator<< (std::ostream &out, const Speed &s)
{
    out << "proc_mappings: ";
    for(auto m : s.proc_mappings)
        out << m << ", ";
    out << endl << "proc_modes:";    
    for(auto m : s.proc_modes)
        out << m << ", ";   
    out << endl << "tdmaAlloc:";    
    for(auto t : s.tdmaAlloc)
        out << t << ", ";
    out << endl << "proc_sched:";    
    for(auto sc : s.proc_sched)
        out << sc << ", ";
    out << endl << "send_sched:";        
    for(auto se : s.send_sched)
        out << se << ", ";
    out << endl << "rec_sched:";        
    for(auto r : s.rec_sched)
        out << r << ", ";        
    return out;
}
std::ostream& operator<< (std::ostream &out, const Particle &particle)
{
    out << "current position: ====================\n" << particle.current_position << endl;
    out << "best position: ====================\n" << particle.best_local_position << endl;
    out << "speed: ====================\n" << particle.speed;
    return out;
}
std::ostream& operator<< (std::ostream &out, const Position &p)
{
    out << "proc_mappings: ";
    for(auto m : p.proc_mappings)
        out << m << " ";
    out << endl << "proc_modes:";    
    for(auto m : p.proc_modes)
        out << m << " ";   
    out << endl << "tdmaAlloc:";    
    for(auto t : p.tdmaAlloc)
        out << t << " ";
    out << endl << "proc_sched -----";    
    for(auto s : p.proc_sched)
        out << s;
    out << endl << "send_sched -----";        
    for(auto s : p.send_sched)
        out << s;
    out << endl << "rec_sched -----";        
    for(auto r : p.rec_sched)
        out << r << " ";    
    out << endl << "fitness -----\n";        
    for(auto f : p.fitness)
        out << f << " ";                
    return out;
}
std::ostream& operator<< (std::ostream &out, const Schedule &sched)
{
    out << endl << "elements:";    
    for(auto e : sched.elements)
        out << e << " ";   
    out << " ->" << sched.dummy;     
    out << endl << "rank:";    
    for(auto r : sched.rank)
        out << r << " ";        
    return out;
}
Schedule::Schedule(vector<int> _elems, int _dummy):
                   elements(_elems),
                   dummy(_dummy)
{
    for(size_t i=0;i<elements.size();i++)
    {
        rank.push_back(i);
    }
    std::random_device rd;
    std::mt19937 g(rd());
 
    std::shuffle(rank.begin(), rank.end(), g);
}
void Schedule::set_rank(int index, int value)
{
    rank[index] = value;
}
void Schedule::set_rank(vector<int> _rank)
{
    if(rank.size() != _rank.size())
        THROW_EXCEPTION(RuntimeException, "rank.size() != _rank.size()" );
        
    for(size_t i=0;i<_rank.size();i++)
        set_rank(i, _rank[i]);
    repair_dist();    
}
int Schedule::get_rank_by_id(int elem_id) const
{
    if((size_t) elem_id >= elements.size())
        THROW_EXCEPTION(RuntimeException, "element is not in the set");
           
    return rank[elem_id];
}
int Schedule::get_rank_by_element(int elem) const
{
    for(size_t i=0;i<rank.size();i++)
    {
        if(elements[i] == elem)
            return rank[i];
    }
    THROW_EXCEPTION(RuntimeException, "element " + tools::toString(elem) + " is not in the set");
           
    return -1;
}
void Schedule::set_rank_by_element(int elem, int _rank) 
{
    for(size_t i=0;i<rank.size();i++)
    {
        if(elements[i] == elem)
        {
            rank[i] = _rank;
            return;
        }
    }
    THROW_EXCEPTION(RuntimeException, "set_rank_by_element: element " + tools::toString(elem) + " is not in the set");
}
vector<int> Schedule::get_rank()
{
    return rank;
}
vector<int> Schedule::get_elements()
{
    return elements;
}

int Schedule::get_next(int elem)
{
    int elem_id = -1;
    ///First find the id of element
    for(size_t i=0;i<elements.size();i++)
    {
        if(elements[i] == elem)
            elem_id = i;
    }
    /**
     * If the input is in the elements list, then we return the element with rank[elem_id]+1
     */ 
    if(elem_id < 0)
        THROW_EXCEPTION(RuntimeException, "element is not in the set");
    
    return get_element_by_rank(rank[elem_id] + 1) ;
    
}
vector<int> Schedule::get_next()
{
    vector<int> next;
    for(auto e: elements)
        next.push_back(get_next(e));        
    return next;    
}
int Schedule::get_element_by_rank(int _rank) const
{
    /**
     * if _rank is the largest rank, then we return the dummy ellement.
     * othersiwe we find the element with the given rank.
     */ 
    if(_rank == (int) rank.size())
        return dummy;
    for(size_t i=0;i<rank.size();i++)
    {
        if(rank[i] == _rank)
            return elements[i];        
    }
    THROW_EXCEPTION(RuntimeException, "could not find the element with input rank="+tools::toString(_rank));
    return -1;
}
vector<int> Schedule::rank_diff(vector<int> _rank)
{
    if(rank.size() != _rank.size())
        THROW_EXCEPTION(RuntimeException, "rank.size() != _rank.size()" );
    vector<int> diff;
    for(size_t i;i<rank.size();i++)
        diff.push_back(rank[i] - _rank[i]);
    
    return diff;    
}
void Schedule::rank_add(vector<float> _speed)
{
    for(size_t i;i<rank.size();i++)
        set_rank(i, rank[i] + random_round(_speed[i]));
}
int Schedule::random_round(float f)
{
  if(random_bool())
    return ceil(f);
  else    
    return floor(f);  
}
void Schedule::switch_ranks(int i, int j)
{
    int tmp = rank[i];
    set_rank(i, rank[j]);
    set_rank(j, tmp);
}
void Schedule::repair_dist()
{
    for(size_t i=0;i<rank.size();i++)
    {
        int cnt = std::count(rank.begin(), rank.end(), rank[i] );
        if(cnt > 1)
        {
            if(random_bool())
            {
                ///Find first unused rank
                size_t j;
                for(j=0;j<rank.size();j++)
                {
                    int cnt_j = std::count(rank.begin(), rank.end(), j);
                    if(cnt_j == 0)
                    {
                        set_rank(i, j);
                        break;
                    }
                }
            }
            else
            {
                ///Find last unused rank
                size_t j;
                for(j=rank.size()-1;j>=0;j--)
                {
                    int cnt_j = std::count(rank.begin(), rank.end(), j);
                    if(cnt_j == 0)
                    {
                        set_rank(i, j);
                        break;
                    }
                }
            }
                        
        }
    }
}
bool Schedule::random_bool()
{
  random_device rnd_device;
  uniform_int_distribution<int> dist(0, 1);
  mt19937 mersenne_engine(rnd_device());  
  auto gen = std::bind(dist, mersenne_engine);
  auto tmp = gen();
  if(tmp == 0)
      return true;
  return false;  
}
