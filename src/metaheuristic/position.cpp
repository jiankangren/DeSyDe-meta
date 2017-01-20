#include "position.hpp"
Position::Position(bool _multi_obj, vector<float> _w):
            multi_obj(_multi_obj),
            weights(_w)
            {
            };
Position::~Position()
{
    proc_sched.clear();
    send_sched.clear();
    rec_sched.clear();            
}
Position::Position(const Position &obj) 
{
    multi_obj = obj.multi_obj;
    proc_mappings = obj.proc_mappings;
    proc_modes = obj.proc_modes;
    tdmaAlloc = obj.tdmaAlloc;
    proc_sched = obj.proc_sched;
    send_sched = obj.send_sched;
    rec_sched = obj.rec_sched;
    fitness = obj.fitness;   
    weights = obj.weights;              
}
void Position::print_multi_obj()
{
    if(multi_obj)
    {
        cout << "multi objective\n";
        return;
    }
    if(!multi_obj)
    {
        cout << "single objective\n";    
        return;
    }
    cout << "unknown objective\n";    
}
bool Position::operator==(const Position& p_in) const
{
    if(multi_obj)
    {
        for(size_t i=0;i<fitness.size();i++)
        {
            if(fitness[i] != p_in.fitness[i])
                return false;
        }
        return true;
    }
    else
    {
        return (fitness_func() == p_in.fitness_func());
    }
}
bool Position::operator!=(const Position& p_in) const
{
    return !(*this == p_in);
}
Position& Position::operator=(const Position& p)
{
    proc_mappings = p.proc_mappings;
    proc_modes = p.proc_modes;
    tdmaAlloc = p.tdmaAlloc;
    fitness = p.fitness;
    weights = p.weights;
    multi_obj = p.multi_obj;
    proc_sched.clear();
    send_sched.clear();
    rec_sched.clear();
    for(auto s: p.proc_sched)
        proc_sched.push_back(std::move(s));
    for(auto s: p.send_sched)
        send_sched.push_back(std::move(s));
    for(auto s: p.rec_sched)
        rec_sched.push_back(std::move(s));               
    return *this;
}

/**
 * Do I dominate p_in?
 */ 
bool Position::dominate(Position& p_in) const
{
    if(empty())
        return false;
    /** -# fitness can not be negative. */            
    if(invalid())
        return false;      
        
    if(p_in.empty() || p_in.invalid())
        return true;
              
    if(multi_obj)
    {
        for(size_t i=0;i<fitness.size();i++)
        {
            if(fitness[i] > p_in.fitness[i])
                return false;
        }
        return true;
    }
    else
    {
        if(fitness_func() < p_in.fitness_func())
        {
            return true;
        }
        else
            return false;    
    }
}
float Position::fitness_func() const
{
    float f = 0;
    for(size_t i=0;i<weights.size();i++)
        f += (float) fitness[i] * weights[i];
    
    return f;
}
bool Position::empty() const
{
    return fitness.empty();
}
bool Position::invalid() const
{
    for(auto f : fitness)
        if(f < 0)
            return true;   
   
   return false;         
}
int Position::weighted_sum(int a, int b, float w) 
{
    float diff = w*(b - a);
    return Schedule::random_round((float) a + diff);
}
int Position::weighted_sum(int a, int b, int c, float w1, float w2) 
{
    float diff1 = w1*(b - a);
    float diff2 = w2*(c - a);
    return Schedule::random_round((float) a + diff1 + diff2);
}
std::ostream& operator<< (std::ostream &out, const Position &p)
{
    out << "proc_mappings: ";
    for(auto m : p.proc_mappings)
        out << m << " ";
    out << endl << "proc_modes:";    
    for(auto m : p.proc_modes)
        out << m << " ";   /*
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
        out << r << " ";    */
    out << endl << "fitness -----\n";        
    for(auto f : p.fitness)
        out << f << " ";                
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
float Schedule::get_relative_rank_by_element(int elem) const
{
    for(size_t i=0;i<rank.size();i++)
    {
        if(elements[i] == elem)
            return ((float)rank[i])/elements.size();
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
     * If _rank is the largest rank, then we return the dummy ellement.
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
            set_rank(i, random_unused_rank());                        
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
int Schedule::random_unused_rank()
{
    vector<int> unused_ranks;
    for(size_t j=0;j<rank.size();j++)
    {
        int cnt_j = std::count(rank.begin(), rank.end(), j);
        if(cnt_j == 0)
        {
            unused_ranks.push_back(j);
        }
    }
    
    random_device rnd_device;
    uniform_int_distribution<int> dist(0, unused_ranks.size()-1);
    mt19937 mersenne_engine(rnd_device());  
    auto gen = std::bind(dist, mersenne_engine);
    auto i = gen();
    return unused_ranks[i];    
}
void Position::opposite()
{
    vector<int> new_proc_mappings = proc_mappings;
    for(size_t i=0;i<proc_mappings.size();i++)
    {
        vector<int> available = opposite_availabe_procs(i, new_proc_mappings);
        if(available.empty())
        {
            /// we choose one random proc
            vector<int> tmp;
            for(size_t j=0;j<proc_modes.size();j++)
                tmp.push_back(j);
            new_proc_mappings[i] = select_random(tmp);                
        }
        else
            new_proc_mappings[i] = select_random(available);                
    }
    proc_mappings = new_proc_mappings;
}
vector<int> Position::actors_by_mapping(int proc)
{
    vector<int> actors;
    for(size_t i=0;i<proc_mappings.size();i++)
    {
        if(proc_mappings[i] == proc)
        {
            actors.push_back(i);
        }
    }
    return actors;
}
vector<int> Position::opposite_availabe_procs(int actor, vector<int> new_proc_mappings)
{
    vector<int> availabe_procs;
    vector<int> co_located_actors = actors_by_mapping(proc_mappings[actor]);
    for(size_t i=0;i<proc_modes.size();i++)
    {
        bool proc_used = false;
        for(auto a: co_located_actors)
        {
            if((int) i == new_proc_mappings[a])                
            {
                proc_used = true;
                break;
            }
        }
        if(!proc_used)
            availabe_procs.push_back(i);
    }       
    return availabe_procs;        
}
int Position::select_random(vector<int> v)
{
    random_device rnd_device;
    uniform_int_distribution<int> dist(0, v.size()-1);
    mt19937 mersenne_engine(rnd_device());  
    auto gen = std::bind(dist, mersenne_engine);
    return v[gen()];    
}

float Speed::average() const
{
    return average(proc_mappings);
}
void Speed::apply_bounds()
{
    float ratio = 2;
    float no_processors = (float)proc_modes.size();
    proc_mappings = bring_v_to_bound(proc_mappings, -no_processors/ratio, no_processors/ratio);
}